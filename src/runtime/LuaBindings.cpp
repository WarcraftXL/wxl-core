// WarcraftXL Lua globals exposed to addons.
// Copyright (C) 2026 WarcraftXL

#include "runtime/LuaBindings.hpp"

#include "core/Hook.hpp"
#include "core/Logger.hpp"
#include "game/Binding.hpp"
#include "game/world/Pick.hpp"
#include "game/world/World.hpp"
#include "offsets/engine/Lua.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <vector>

namespace wxl::runtime::lua
{
    namespace
    {
        namespace loff = wxl::offsets::engine::lua;
        namespace world = wxl::game::world;
        namespace woff = wxl::offsets::game::world;

        void* g_installedState = nullptr;
        bool g_validatorActive = false;
        loff::ValidateFunctionPointerFn g_origValidateFunctionPointer = nullptr;

        struct FunctionEntry
        {
            std::string name;
            Callback function = nullptr;
        };

        struct ScriptEntry
        {
            std::string name;
            std::string source;
        };

        struct CVarEntry
        {
            std::string name;
            std::string defaultValue;
        };

        std::vector<FunctionEntry>& Functions()
        {
            static std::vector<FunctionEntry> entries;
            return entries;
        }

        std::vector<ScriptEntry>& Scripts()
        {
            static std::vector<ScriptEntry> entries;
            return entries;
        }

        std::vector<CVarEntry>& CVars()
        {
            static std::vector<CVarEntry> entries;
            return entries;
        }

        bool AddFunction(const char* name, Callback function)
        {
            if (!name || !*name || !function) return false;
            auto& entries = Functions();
            const auto existing = std::find_if(entries.begin(), entries.end(), [name](const FunctionEntry& entry) {
                return _stricmp(entry.name.c_str(), name) == 0;
            });
            if (existing != entries.end()) return existing->function == function;
            entries.push_back(FunctionEntry{name, function});
            return true;
        }

        /** Lua: x, y, z, guidLow, guidHigh = GetMouseWorldPosition(). */
        int __cdecl GetMouseWorldPosition(void* state)
        {
            if (!state) return 0;

            __try
            {
                world::WorldHit hit{};
                if (!world::PickCursor(hit)) return 0;

                auto pushNumber = wxl::game::Native<loff::LuaPushNumberFn>(loff::kLuaPushNumber);
                pushNumber(state, static_cast<double>(hit.pos.x));
                pushNumber(state, static_cast<double>(hit.pos.y));
                pushNumber(state, static_cast<double>(hit.pos.z));
                pushNumber(state, static_cast<double>(
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hit.objLo))));
                pushNumber(state, static_cast<double>(
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hit.objHi))));
                return 5;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                // Loading screens and world teardown can invalidate the world frame between the
                // initial null check and the native intersection. A miss is the safe Lua result.
                return 0;
            }
        }

        /** Lua: x, y, z = GetUnitPosition(guidLow, guidHigh). */
        int __cdecl GetUnitPosition(void* state)
        {
            if (!state) return 0;

            __try
            {
                auto toNumber = wxl::game::Native<loff::LuaToNumberFn>(loff::kLuaToNumber);
                const uint32_t low = static_cast<uint32_t>(toNumber(state, 1));
                const uint32_t high = static_cast<uint32_t>(toNumber(state, 2));
                const unsigned long long guid =
                    (static_cast<unsigned long long>(high) << 32) | low;
                if (!guid) return 0;

                void* unit = world::ResolveObject(
                    guid, world::kTypeMaskUnit | world::kTypeMaskPlayer);
                if (!unit) return 0;

                float position[3] = {};
                world::UnitPosition(unit, position);
                auto pushNumber = wxl::game::Native<loff::LuaPushNumberFn>(loff::kLuaPushNumber);
                pushNumber(state, static_cast<double>(position[0]));
                pushNumber(state, static_cast<double>(position[1]));
                pushNumber(state, static_cast<double>(position[2]));
                return 3;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                // The object can despawn between GUID resolution and its position read.
                return 0;
            }
        }

        bool ProjectWorldToScreen(float x, float y, float z,
                                  float& screenX, float& screenY, float& depth,
                                  bool& visible)
        {
            __try
            {
                void* worldFrame = *reinterpret_cast<void**>(woff::kWorldFrame);
                if (!worldFrame) return false;

                const float worldPos[3] = { x, y, z };
                float projected[3] = {};
                uint32_t clipFlags = 0;
                const int nativeVisible = wxl::game::Native<woff::GetScreenCoordinatesFn>(
                    woff::kGetScreenCoordinates)(worldFrame, nullptr, worldPos, projected, &clipFlags);
                visible = nativeVisible != 0;

                // Match Duskhaven/Blizzard's UI-space conversion. Both axes deliberately use
                // the same UI aspect multiplier; this is not a D3D viewport pixel conversion.
                const float alpha1 = *reinterpret_cast<const float*>(
                    woff::kUiTexCoordAlphaMultiplier1);
                const float alpha3 = *reinterpret_cast<const float*>(
                    woff::kUiTexCoordAlphaMultiplier3);
                if (alpha1 == 0.0f) return false;

                const float uiScale = (alpha3 * 1024.0f) / alpha1;
                screenX = projected[0] * uiScale;
                screenY = projected[1] * uiScale;
                depth = projected[2];
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        /** Lua: screenX, screenY, depth, visible = ConvertCoordsToScreenSpace(x, y, z). */
        int __cdecl ConvertCoordsToScreenSpace(void* state)
        {
            if (!state) return 0;

            __try
            {
                // Lua 5.1's permissive numeric conversion matches stock WoW API conventions.
                auto toNumber = wxl::game::Native<loff::LuaToNumberFn>(loff::kLuaToNumber);
                float screenX = 0.0f, screenY = 0.0f, depth = 0.0f;
                bool visible = false;
                if (!ProjectWorldToScreen(
                        static_cast<float>(toNumber(state, 1)),
                        static_cast<float>(toNumber(state, 2)),
                        static_cast<float>(toNumber(state, 3)), screenX, screenY, depth, visible))
                    return 0;

                auto pushNumber = wxl::game::Native<loff::LuaPushNumberFn>(loff::kLuaPushNumber);
                pushNumber(state, static_cast<double>(screenX));
                pushNumber(state, static_cast<double>(screenY));
                pushNumber(state, static_cast<double>(depth));
                pushNumber(state, visible ? 1.0 : 0.0);
                return 4;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return 0;
            }
        }

        void EnsureBuiltins()
        {
            static bool registered = false;
            if (registered) return;
            registered = true;
            AddFunction("GetMouseWorldPosition", &GetMouseWorldPosition);
            AddFunction("GetUnitPosition", &GetUnitPosition);
            AddFunction("ConvertCoordsToScreenSpace", &ConvertCoordsToScreenSpace);
        }

        std::string QuoteLua(const std::string& value)
        {
            std::string out;
            out.reserve(value.size() + 2);
            out.push_back('"');
            for (const unsigned char ch : value)
            {
                if (ch == '\\' || ch == '"') { out.push_back('\\'); out.push_back(static_cast<char>(ch)); }
                else if (ch == '\n') out += "\\n";
                else if (ch == '\r') out += "\\r";
                else if (ch == '\0') out += "\\000";
                else out.push_back(static_cast<char>(ch));
            }
            out.push_back('"');
            return out;
        }

        void Execute(void* state, const std::string& source, const char* label)
        {
            if (!state || source.empty()) return;
            __try
            {
                wxl::game::Native<loff::FrameScriptExecuteFn>(loff::kFrameScriptExecute)(source.c_str(), state);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                WLOG_ERROR("lua: external source crashed label='%s'", label ? label : "?");
            }
        }

        /** Keeps Blizzard's pointer validation intact except for WXL's registered callbacks. */
        void __cdecl ValidateFunctionPointer(uintptr_t function)
        {
            for (const FunctionEntry& entry : Functions())
                if (function == reinterpret_cast<uintptr_t>(entry.function)) return;
            if (g_origValidateFunctionPointer) g_origValidateFunctionPointer(function);
        }
    }

    bool RegisterFunction(const char* name, Callback function)
    {
        EnsureBuiltins();
        const bool added = AddFunction(name, function);
        if (!added) WLOG_ERROR("lua: rejected function registration name='%s'", name ? name : "");
        return added;
    }

    bool RegisterScript(const char* name, const char* source)
    {
        if (!name || !*name || !source || !*source) return false;
        auto& entries = Scripts();
        const auto existing = std::find_if(entries.begin(), entries.end(), [name](const ScriptEntry& entry) {
            return _stricmp(entry.name.c_str(), name) == 0;
        });
        if (existing != entries.end()) return existing->source == source;
        entries.push_back(ScriptEntry{name, source});
        return true;
    }

    bool RegisterCVar(const char* name, const char* defaultValue)
    {
        if (!name || !*name || !defaultValue) return false;
        const unsigned char first = static_cast<unsigned char>(*name);
        if (!(std::isalpha(first) || first == '_')) return false;
        for (const unsigned char ch : std::string(name))
            if (!(std::isalnum(ch) || ch == '_' || ch == '.' || ch == '-')) return false;
        auto& entries = CVars();
        const auto existing = std::find_if(entries.begin(), entries.end(), [name](const CVarEntry& entry) {
            return _stricmp(entry.name.c_str(), name) == 0;
        });
        if (existing != entries.end()) return existing->defaultValue == defaultValue;
        entries.push_back(CVarEntry{name, defaultValue});
        return true;
    }

    int __cdecl GetTop(void* state)
    {
        return state ? wxl::game::Native<loff::LuaGetTopFn>(loff::kLuaGetTop)(state) : 0;
    }

    int __cdecl IsNumber(void* state, int index)
    {
        return state ? wxl::game::Native<loff::LuaIsNumberFn>(loff::kLuaIsNumber)(state, index) : 0;
    }

    int __cdecl IsString(void* state, int index)
    {
        return state ? wxl::game::Native<loff::LuaIsStringFn>(loff::kLuaIsString)(state, index) : 0;
    }

    double __cdecl ToNumber(void* state, int index)
    {
        return state ? wxl::game::Native<loff::LuaToNumberFn>(loff::kLuaToNumber)(state, index) : 0.0;
    }

    const char* __cdecl ToString(void* state, int index, uint32_t* length)
    {
        size_t nativeLength = 0;
        const char* value = state
            ? wxl::game::Native<loff::LuaToStringFn>(loff::kLuaToString)(state, index, &nativeLength)
            : nullptr;
        if (length) *length = static_cast<uint32_t>(std::min<size_t>(
            nativeLength, (std::numeric_limits<uint32_t>::max)()));
        return value;
    }

    void __cdecl PushNumber(void* state, double value)
    {
        if (state) wxl::game::Native<loff::LuaPushNumberFn>(loff::kLuaPushNumber)(state, value);
    }

    void __cdecl PushString(void* state, const char* value)
    {
        if (state) wxl::game::Native<loff::LuaPushStringFn>(loff::kLuaPushString)(state, value ? value : "");
    }

    void __cdecl PushBoolean(void* state, int value)
    {
        if (state) wxl::game::Native<loff::LuaPushBooleanFn>(loff::kLuaPushBoolean)(state, value != 0);
    }

    void __cdecl PushNil(void* state)
    {
        if (state) wxl::game::Native<loff::LuaPushNilFn>(loff::kLuaPushNil)(state);
    }

    bool ExecuteCurrent(const char* label, const char* source)
    {
        if (!source || !*source) return false;
        void* state = wxl::game::Native<loff::FrameScriptGetContextFn>(
            loff::kFrameScriptGetContext)();
        if (!state) return false;
        Execute(state, source, label);
        return true;
    }

    void InstallHooks()
    {
        EnsureBuiltins();
        wxl::core::hook::Install(
            "LuaValidateFunctionPointer", loff::kValidateFunctionPointer,
            reinterpret_cast<void*>(&ValidateFunctionPointer),
            reinterpret_cast<void**>(&g_origValidateFunctionPointer));
    }

    void Activate()
    {
        g_validatorActive = g_origValidateFunctionPointer != nullptr;
    }

    void Install(bool force)
    {
        if (!g_validatorActive) return;

        // Callers are main-thread world/input hooks, keeping Lua table mutation on WoW's thread.
        void* state = wxl::game::Native<loff::FrameScriptGetContextFn>(
            loff::kFrameScriptGetContext)();
        if (!state) return;
        if (!force && state == g_installedState) return;

        const bool contextChanged = state != g_installedState;
        const auto registerFunction = wxl::game::Native<loff::FrameScriptRegisterFunctionFn>(
            loff::kFrameScriptRegisterFunction);
        for (const FunctionEntry& entry : Functions())
            registerFunction(entry.name.c_str(), entry.function);

        if (contextChanged)
        {
            for (const CVarEntry& entry : CVars())
            {
                // GlueXML uses a separate restricted Lua environment and does not publish the
                // in-world RegisterCVar global. Skip it there; a new FrameScript state is detected
                // on world entry and receives the same registration list automatically.
                std::string source = "if type(RegisterCVar)=='function' then RegisterCVar(";
                source += QuoteLua(entry.name);
                source += ",";
                source += QuoteLua(entry.defaultValue);
                source += ") end";
                Execute(state, source, entry.name.c_str());
            }
            for (const ScriptEntry& entry : Scripts())
                Execute(state, entry.source, entry.name.c_str());
        }
        g_installedState = state;
        if (contextChanged)
            WLOG_INFO("lua: installed functions=%zu scripts=%zu cvars=%zu state=%p",
                      Functions().size(), Scripts().size(), CVars().size(), state);
    }
}
