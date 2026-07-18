// GUID-backed object proxies: the userdata + metatable machinery behind wxl Unit objects.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#include "engine/lua/ObjectProxy.hpp"

#include "engine/lua/LuaJit.hpp"
#include "game/Binding.hpp"
#include "game/camera/Camera.hpp"
#include "game/gx/Gx.hpp"
#include "game/unit/Unit.hpp"
#include "offsets/game/Unit.hpp"
#include "offsets/game/World.hpp"

#include <windows.h>

#include <cstdio>

namespace wxl::lua
{
    namespace
    {
        namespace off = wxl::offsets::game::unit;

        // Registry key of the weak-valued cache table {guidBytes -> Unit userdata} that interns
        // proxy identity. Keyed by the GUID's raw 8 bytes because a Lua number (double) cannot hold
        // a full u64 losslessly.
        constexpr char kProxyCacheKey[] = "wxl.proxycache";

        // --- metamethods ---

        // Equality by GUID. Lua 5.1 only invokes __eq when both operands share this metatable, so
        // both userdata are known-good Units here.
        int L_eq(lua_State* L)
        {
            const auto* a = static_cast<ObjectProxy*>(luaL_checkudata(L, 1, kUnitMeta));
            const auto* b = static_cast<ObjectProxy*>(luaL_checkudata(L, 2, kUnitMeta));
            lua_pushboolean(L, a->guid == b->guid);
            return 1;
        }

        // "Unit:0x<16 hex>". lua_pushfstring lacks a 64-bit integer format, so format by hand.
        int L_tostring(lua_State* L)
        {
            const auto* p = static_cast<ObjectProxy*>(luaL_checkudata(L, 1, kUnitMeta));
            char buf[32];
            std::snprintf(buf, sizeof(buf), "Unit:0x%016llX",
                          static_cast<unsigned long long>(p->guid));
            lua_pushstring(L, buf);
            return 1;
        }
    } // namespace

    void RegisterMetatables(lua_State* L)
    {
        // Weak-valued cache: a proxy that Lua no longer references is collected and drops out.
        lua_newtable(L);                        // [cache]
        lua_newtable(L);                        // [cache, mt]
        lua_pushstring(L, "v");
        lua_setfield(L, -2, "__mode");          // weak values
        lua_setmetatable(L, -2);                // [cache]
        lua_setfield(L, LUA_REGISTRYINDEX, kProxyCacheKey);

        // Unit metatable: identity/printing metamethods, a locked __metatable, and a method table
        // under __index that the Unit method context fills in later at Register().
        luaL_newmetatable(L, kUnitMeta);        // [mt]
        lua_pushcfunction(L, &L_eq);
        lua_setfield(L, -2, "__eq");
        lua_pushcfunction(L, &L_tostring);
        lua_setfield(L, -2, "__tostring");
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "__metatable");     // getmetatable() from Lua sees false, not the table
        lua_newtable(L);                        // [mt, methods]
        lua_setfield(L, -2, "__index");         // mt.__index = methods
        lua_pop(L, 1);                          // []
    }

    void PushUnit(lua_State* L, uint64_t guid)
    {
        if (guid == 0)
        {
            lua_pushnil(L);
            return;
        }

        lua_getfield(L, LUA_REGISTRYINDEX, kProxyCacheKey);   // [cache]
        lua_pushlstring(L, reinterpret_cast<const char*>(&guid), sizeof(guid)); // [cache, key]
        lua_pushvalue(L, -1);                                 // [cache, key, key]
        lua_rawget(L, -3);                                    // [cache, key, cached|nil]
        if (!lua_isnil(L, -1))
        {
            lua_replace(L, -3);                               // [cached, key]
            lua_pop(L, 1);                                    // [cached]
            return;
        }
        lua_pop(L, 1);                                        // [cache, key]

        auto* p  = static_cast<ObjectProxy*>(lua_newuserdata(L, sizeof(ObjectProxy))); // [cache, key, ud]
        p->guid  = guid;
        p->type  = ObjectType::Unit;
        luaL_getmetatable(L, kUnitMeta);                      // [cache, key, ud, mt]
        lua_setmetatable(L, -2);                              // [cache, key, ud]

        lua_pushvalue(L, -2);                                 // [cache, key, ud, key]
        lua_pushvalue(L, -2);                                 // [cache, key, ud, key, ud]
        lua_rawset(L, -5);                                    // cache[key] = ud -> [cache, key, ud]
        lua_replace(L, -3);                                   // [ud, key]
        lua_pop(L, 1);                                        // [ud]
    }

    uint64_t CheckGuid(lua_State* L, int idx)
    {
        void* ud = luaL_checkudata(L, idx, kUnitMeta);
        return static_cast<ObjectProxy*>(ud)->guid;
    }

    void* ResolveObject(uint64_t guid, uint32_t typemask)
    {
        if (guid == 0)
            return nullptr;
        __try
        {
            return wxl::game::Native<off::GetObjectFn>(off::kGetObjectByGuid)(guid, typemask, "wxl", 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void* ResolveUnit(uint64_t guid)
    {
        return ResolveObject(guid, off::kTypeMaskUnit | off::kTypeMaskPlayer);
    }

    uint64_t ReadGuidAt(uintptr_t addr)
    {
        __try
        {
            return *reinterpret_cast<const uint64_t*>(addr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    uint64_t ActivePlayerGuid()
    {
        __try
        {
            return wxl::game::Native<off::ActivePlayerGuidFn>(off::kActivePlayerGuid)();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool ReadPosition(void* unit, float out[3])
    {
        if (!unit)
            return false;
        __try
        {
            const auto* u = static_cast<off::UnitObject*>(unit);
            out[0] = u->position[0];
            out[1] = u->position[1];
            out[2] = u->position[2];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadReaction(void* self, void* other, int* out)
    {
        if (!self || !other)
            return false;
        __try
        {
            *out = wxl::game::unit::Reaction(self, other);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool WorldToScreenPixels(const float pos[3], float& sx, float& sy, bool& visible)
    {
        __try
        {
            namespace woff = wxl::offsets::game::world;

            // Native world->screen projection -- the exact call the client uses for its own
            // 3D-anchored UI (world text, chat bubbles, target indicators), so it is correct
            // under every camera pitch/yaw/distance. Nonzero return = point is on-screen.
            void* worldFrame = *reinterpret_cast<void**>(woff::kWorldFrame);
            if (!worldFrame)
                return false;

            float projected[3] = {};
            uint32_t clipFlags = 0;
            const int onScreen = wxl::game::Native<woff::GetScreenCoordinatesFn>(
                woff::kGetScreenCoordinates)(worldFrame, nullptr, pos, projected, &clipFlags);

            // Live render viewport for ImGui pixel space.
            wxl::game::gx::Device9 dev = wxl::game::gx::Device();
            if (!dev)
                return false;
            // D3DVIEWPORT9: {X, Y, Width, Height (u32 each), MinZ, MaxZ (f32)} = 24 bytes.
            uint32_t vp[6] = {};
            dev.GetViewport(vp);
            const float w = static_cast<float>(vp[2]);
            const float h = static_cast<float>(vp[3]);
            if (w <= 0.0f || h <= 0.0f)
                return false;

            // projected[0..1] are DDC (device) pixel coordinates with a BOTTOM-LEFT origin (verified
            // in-game: the raw Y tracks perfectly once flipped, for any camera pitch/yaw). Scale DDC
            // pixels to the live viewport (== 1.0 when DDC matches the viewport) and flip Y to reach
            // ImGui's top-left origin.
            const float ddcW = *reinterpret_cast<const float*>(woff::kDdcWidth);
            const float ddcH = *reinterpret_cast<const float*>(woff::kDdcHeight);
            if (ddcW <= 0.0f || ddcH <= 0.0f)
                return false;

            sx = projected[0] * (w / ddcW);
            sy = h - projected[1] * (h / ddcH); // bottom-left -> top-left (ImGui)

            visible = (onScreen != 0) && sx >= 0.0f && sx <= w && sy >= 0.0f && sy <= h;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}
