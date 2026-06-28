// modern-anim runtime face: normalize native-served external M2 animation files before rebase.
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

#include "../shared/ModernAnim.hpp"

#include "core/Hook.hpp"
#include "core/Logger.hpp"
#include "events/EventScript.hpp"
#include "offsets/game/M2.hpp"

#include <windows.h>

#include <mutex>

namespace wxl::scripts::modernanim
{
    namespace ev = wxl::events;
    namespace m2 = wxl::offsets::game::m2;

    namespace
    {
        std::mutex g_installMutex;
        bool g_installed = false;
        m2::M2_AnimLoadCompleteFn g_origAnimLoadComplete = nullptr;

        void __cdecl hkAnimLoadComplete(void* node)
        {
            void* buffer = nullptr;
            uint32_t size = 0;
            m2::IoRecord* record = nullptr;

            __try
            {
                auto* load = static_cast<m2::LoadNode*>(node);
                record = load ? static_cast<m2::IoRecord*>(load->record) : nullptr;
                if (record)
                {
                    buffer = record->buffer;
                    size = record->size;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                buffer = nullptr;
                size = 0;
            }

            if (buffer && size)
            {
                __try
                {
                    uint32_t rewrittenSize = size;
                    if (ProcessInPlaceAndResize(buffer, rewrittenSize) && rewrittenSize != size && record)
                        record->size = rewrittenSize;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                }
            }

            g_origAnimLoadComplete(node);
        }

        bool InstallHook(bool enableNow)
        {
            std::lock_guard<std::mutex> lock(g_installMutex);
            if (g_installed) return true;

            if (!wxl::core::hook::Install("M2AnimLoadComplete", m2::kAnimLoadComplete,
                                          reinterpret_cast<void*>(&hkAnimLoadComplete),
                                          reinterpret_cast<void**>(&g_origAnimLoadComplete)))
                return false;

            g_installed = true;
            WLOG_INFO("modern-anim: external animation hook installed");

            if (enableNow)
                wxl::core::hook::EnableAll();
            return true;
        }

        class ModernAnimRuntime final : public ev::EventScript
        {
        public:
            ModernAnimRuntime()
            {
                on<&ModernAnimRuntime::OnBeforeHostLaunch>(ev::Event::OnBeforeHostLaunch);
                on<&ModernAnimRuntime::OnModelLoadPre>(ev::Event::OnModelLoadPre);
                WLOG_INFO("wxl-modern-anim: loaded (external M2 animation support)");
            }

            void OnBeforeHostLaunch(const ev::HostLaunchArgs& a)
            {
                (void)a;
                InstallHook(false);
            }

            void OnModelLoadPre(const ev::ModelLoadArgs& a)
            {
                (void)a;
                InstallHook(true);
            }
        };

        ModernAnimRuntime g_runtime;
    }
}
