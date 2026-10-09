// Discovery and loading of out-of-core extensions.
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

#include "runtime/Extensions.hpp"

#include "wxl/PluginApi.h"

#include "common/Log.hpp"
#include "engine/events/Event.hpp"
#include "engine/hook/Hook.hpp"
#include "engine/ui/ImGuiHost.hpp"
#include "game/Boot.hpp"
#include "runtime/HookPoints.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace wxl::runtime::extensions
{
    namespace
    {
        // --- the service table --------------------------------------------------------------
        // Arguments arrive from a binary this build never saw, so a null or an out-of-range value is
        // ordinary input rather than a precondition an assert could enforce.

        void __cdecl ApiLog(int level, const char* tag, const char* fmt, ...)
        {
            const int clamped = level < WXL_LOG_TRACE ? WXL_LOG_TRACE
                              : level > WXL_LOG_ERROR ? WXL_LOG_ERROR
                                                      : level;
            const log::Level severity = log::Level(clamped);
            if (!fmt || !log::Enabled(severity)) return;

            // Formatted here, so a foreign format string never reaches the sink unbounded.
            char line[1024];
            va_list args;
            va_start(args, fmt);
            std::vsnprintf(line, sizeof line, fmt, args);
            va_end(args);

            log::Write(severity, "%s: %s", tag ? tag : "extension", line);
        }

        void __cdecl ApiSubscribe(uint32_t event, WXL_EventFn handler, void* user)
        {
            if (!handler || event >= uint32_t(events::Event::Count))
            {
                WLOG_ERROR("extensions: subscribe to unknown event %u ignored", event);
                return;
            }
            // No cast on handler: WXL_EventFn is events::Handler with the convention spelled out, so
            // a core built with a different default would fail here rather than silently mismatch.
            events::Subscribe(events::Event(event), handler, user);
        }

        void __cdecl ApiEmit(uint32_t event, const void* args)
        {
            if (event >= uint32_t(events::Event::Count))
            {
                WLOG_ERROR("extensions: emit of unknown event %u ignored", event);
                return;
            }
            events::Emit(events::Event(event), args);
        }

        int __cdecl ApiHookAttach(const char* name, uintptr_t target, void* detour, void** original,
                                  int priority)
        {
            if (!target || !detour || !original) return 0;
            return hook::Install(name ? name : "extension", target, detour, original, priority) ? 1 : 0;
        }

        int __cdecl ApiHookAttachByName(const char* pointName, void* detour, void** original,
                                        int priority)
        {
            if (!detour || !original) return 0;
            return hookpoints::AttachByName(pointName, detour, original, priority);
        }

        /// A service one extension offers another. Name, version and pointer are stored without
        /// being interpreted, so two extensions can agree on a capability this table does not model.
        struct Service
        {
            std::string name;
            uint32_t    version;
            void*       iface;
        };

        std::vector<Service>& Services()
        {
            static std::vector<Service> services;
            return services;
        }

        void __cdecl ApiPublishInterface(const char* name, uint32_t version, void* iface)
        {
            if (!name || !iface) return;
            Services().push_back(Service{name, version, iface});
            WLOG_DEBUG("extensions: interface '%s' v%u published", name, version);
        }

    }

    // Same effect as an extension's own PublishInterface call, but reachable from the CORE side (e.g.
    // the M2 arena's Boot-phase reservation, which has to publish before any extension exists to call
    // GetInterface). Not exported through WXL_Api: an extension always reaches this through
    // ApiPublishInterface instead.
    void PublishInterface(const char* name, uint32_t version, void* iface)
    {
        ApiPublishInterface(name, version, iface);
    }

    namespace
    {

        void* __cdecl ApiGetInterface(const char* name, uint32_t version)
        {
            if (!name) return nullptr;
            for (const Service& service : Services())
                if (service.version == version && service.name == name) return service.iface;
            return nullptr;
        }

        const WXL_Api g_api = {
            sizeof(WXL_Api),
            WXL_API_VERSION,
            &ApiLog,
            &ApiSubscribe,
            &ApiEmit,
            &ApiHookAttach,
            &ApiHookAttachByName,
            &ApiPublishInterface,
            &ApiGetInterface,
            // Straight through to the overlay host: it already owns the panel registry and the one
            // point in the frame where drawing a control is legal, so nothing is re-decided here.
            &wxl::ui::c::AddPanel,
            &wxl::ui::c::IsOpen,
            &wxl::ui::c::Text,
            &wxl::ui::c::Separator,
            &wxl::ui::c::Button,
            &wxl::ui::c::Checkbox,
            &wxl::ui::c::SliderFloat,
            &wxl::ui::c::SliderInt,
            &wxl::ui::c::ColorEdit,
            &wxl::ui::c::SameLine,
            &wxl::ui::c::Combo,
            &wxl::ui::c::CollapsingHeader,
            &wxl::ui::c::InputText,
            &wxl::ui::c::TextWrapped,   // appended in WXL_Api order (after UiInputText) ...
            &wxl::ui::c::SetPanelOpen,  // ... then UiSetPanelOpen / UiIsPanelOpen
            &wxl::ui::c::IsPanelOpen,
            &wxl::ui::c::PanelCount,    // ... then UiPanelCount / UiPanelTitle
            &wxl::ui::c::PanelTitle,
            &wxl::ui::c::TextColored,   // ... then UiTextColored
        };

        // --- loading --------------------------------------------------------------------------

        /// Resolves one export as its declared function type.
        template <class Fn>
        Fn Export(HMODULE module, const char* name)
        {
            return reinterpret_cast<Fn>(GetProcAddress(module, name));
        }

        /**
         * @brief Loads and initialises one extension.
         *
         * Everything that can disqualify it is checked before WXL_Load runs: an extension built
         * against another API version or client build is refused having executed nothing.
         * @param folder  the extension's folder name, used for logging before its own name is known.
         * @param path    full path to its DLL.
         * @return true if the extension loaded and initialised.
         */
        bool LoadOne(const char* folder, const std::string& path)
        {
            HMODULE module = LoadLibraryA(path.c_str());
            if (!module)
            {
                WLOG_ERROR("extensions: '%s' could not be loaded (win32 %lu)", folder, GetLastError());
                return false;
            }

            const auto query = Export<WXL_QueryFn>(module, "WXL_Query");
            const auto load  = Export<WXL_LoadFn>(module, "WXL_Load");
            if (!query || !load)
            {
                WLOG_ERROR("extensions: '%s' exports no WXL_Query/WXL_Load", folder);
                FreeLibrary(module);
                return false;
            }

            const WXL_PluginInfo* info = query();
            if (!info || info->structSize != sizeof(WXL_PluginInfo))
            {
                WLOG_ERROR("extensions: '%s' returned no usable info block", folder);
                FreeLibrary(module);
                return false;
            }

            const char* name = info->name ? info->name : folder;
            if (info->apiVersion != WXL_API_VERSION)
            {
                WLOG_ERROR("extensions: '%s' was built against API v%u, this core serves v%u",
                           name, info->apiVersion, WXL_API_VERSION);
                FreeLibrary(module);
                return false;
            }
            if (info->clientBuild != WXL_CLIENT_BUILD)
            {
                WLOG_ERROR("extensions: '%s' targets client build %u, this one is %u",
                           name, info->clientBuild, WXL_CLIENT_BUILD);
                FreeLibrary(module);
                return false;
            }

            if (!load(&g_api))
            {
                // Left loaded: WXL_Load may already have attached a detour or taken a subscription
                // before giving up, and unloading would leave the core pointing into freed code.
                WLOG_ERROR("extensions: '%s' declined to initialise", name);
                return false;
            }

            WLOG_INFO("extensions: loaded '%s' v%u", name, info->pluginVersion);
            return true;
        }

        /// @return the number of extensions that loaded.
        int LoadAll()
        {
            std::vector<std::string> folders;

            WIN32_FIND_DATAA entry{};
            HANDLE search = FindFirstFileA("Extensions\\*", &entry);
            if (search == INVALID_HANDLE_VALUE)
            {
                WLOG_DEBUG("extensions: no Extensions folder");
                return 0;
            }
            do
            {
                if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (entry.cFileName[0] == '.') continue;
                folders.emplace_back(entry.cFileName);
            } while (FindNextFileA(search, &entry));
            FindClose(search);

            // Enumeration order is a filesystem detail; load order has to be reproducible.
            std::sort(folders.begin(), folders.end());

            int loaded = 0;
            for (const std::string& folder : folders)
            {
                const std::string path = "Extensions\\" + folder + "\\" + folder + ".dll";
                if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
                {
                    WLOG_WARN("extensions: '%s' holds no %s.dll", folder.c_str(), folder.c_str());
                    continue;
                }
                if (LoadOne(folder.c_str(), path)) ++loaded;
            }

            if (!folders.empty()) WLOG_INFO("extensions: %d loaded", loaded);
            return loaded;
        }

        game::boot::EngineInitFn g_origEngineInit = nullptr;

        /**
         * @brief Loads the extensions, then lets engine initialisation proceed.
         *
         * Loading here rather than from the deferred main thread is what makes the ordering a fact
         * instead of a race: the client reaches this point on its own, before the reader queues and
         * the texture scratch exist. The batch is enabled immediately because the core's own was
         * armed back in DllMain, so nothing else will arm what the extensions just attached.
         */
        uint32_t __cdecl EngineInitDetour()
        {
            static bool loaded = false;
            if (!loaded)
            {
                loaded = true;
                LoadAll();
                hook::EnableAll();
            }
            return g_origEngineInit();
        }
    }

    bool InstallLoader()
    {
        return hook::Install("extensions-loader", game::boot::kEngineInit,
                             &EngineInitDetour, &g_origEngineInit);
    }
}
