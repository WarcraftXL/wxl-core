// WMO runtime guards: portal / group-resident cull guards on live WMO objects.
// Copyright (C) 2026 WraithEngine
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

#include "WmoGuards.hpp"

#include "WmoMultiPass.hpp"
#include "WMO.hpp"
#include "Hook.hpp"
#include "Logger.hpp"

#include <windows.h>
#include <cstdint>

namespace off = wraith::offsets::game::wmo;

namespace wraith::runtime::wmo
{
    namespace
    {
        // Material/texture resolver: native this-in-ECX(model, materialIndex); declared __fastcall with a
        // dummy edx so the trampoline keeps materialIndex on the stack as the original expects.
        using MatResolveFn = void(__fastcall*)(void* model, void* edx, int materialIndex);
        // Portal-visibility traversal: native this-in-ECX(model, groupIndex, a, b, out); declared __fastcall
        // with a dummy edx so the trampoline keeps groupIndex and the rest on the stack as the original expects.
        using PortalVisFn  = unsigned int(__fastcall*)(void* model, void* edx, unsigned int groupIndex,
                                                       float* a, float* b, unsigned int* out);
        // Group-resident accessor: native this-in-ECX(model, groupIndex, force); declared __fastcall with a
        // dummy edx so the trampoline keeps groupIndex and force on the stack as the original expects.
        using GroupResidentFn = unsigned int(__fastcall*)(void* model, void* edx, unsigned int groupIndex,
                                                          unsigned int force);

        MatResolveFn    g_origMatResolve    = nullptr;
        PortalVisFn     g_origPortalVis     = nullptr;
        GroupResidentFn g_origGroupResident = nullptr;
        off::Wmo_GroupBatchDrawFn g_origBatchDraw = nullptr;

        // Per-batch draw guard. The native draw walks the group's parent root (grp+0x18C) -> material base
        // (root+0x160) -> per-material/per-batch fields with NO validity checks, and faults when any of those
        // is garbage (AV @0x007AC776/0x7AC77E: a valid root whose material-base pointer is junk, etc.). The
        // garbage comes from a specific WMO whose material/batch data does not survive our down-convert cleanly;
        // a narrow pointer probe just chases the fault one deref deeper, so wrap the whole native draw in SEH
        // (same approach as the portal-visibility guard) - any fault aborts THIS group's draw for the frame
        // instead of crashing. A WMO that faults every frame shows up in the log so its data can be fixed.
        void __stdcall BatchDrawGuard(int groupObj, int pass)
        {
            bool drew = false;
            __try
            {
                g_origBatchDraw(groupObj, pass);
                drew = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                static int s_log = 0;
                if (s_log < 20) { ++s_log; WLOG_INFO("WMO batch-draw caught (group=%p pass=%d)", reinterpret_cast<void*>(groupObj), pass); }
            }

            // Compose any dropped layer textures (shine / emissive) over the base draw, geometry still bound.
            if (drew)
                multipass::RunExtraPasses(groupObj);
        }

        // Read the model's material count through SEH. Returns false (treated as count 0) on a fault.
        bool ReadMaterialCount(void* model, uint32_t& count)
        {
            __try
            {
                count = *reinterpret_cast<uint32_t*>(static_cast<char*>(model) + off::kOffMaterialCount);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // Material/texture resolver guard: the native resolver indexes the material table by materialIndex
        // with no bounds check, so an index past the material count reads a wild texture-name offset and
        // faults. Skip the original when the index is out of range (or the count is unreadable / zero).
        void __fastcall MatResolveDetour(void* model, void* edx, int materialIndex)
        {
            uint32_t count = 0;
            const bool ok = model && ReadMaterialCount(model, count);
            if (!ok || count == 0 || materialIndex < 0 || static_cast<uint32_t>(materialIndex) >= count)
            {
                static int s_guardLog = 0;
                if (s_guardLog < 20)
                {
                    ++s_guardLog;
                    WLOG_INFO("WMO mat-guard: idx=%d >= count=%d", materialIndex, count);
                }
                return; // do not index past the material table
            }
            g_origMatResolve(model, edx, materialIndex);
        }

        // Portal-visibility traversal guard: the native traversal dereferences a group runtime object looked
        // up by group index with no null check, so an unloaded group's NULL/garbage array entry faults. Run
        // the original under SEH; on a fault degrade to "not visible through this portal" (return 0) so the
        // client survives. The __try body holds only the call + return (no C++ objects) to satisfy SEH.
        unsigned int __fastcall PortalVisDetour(void* model, void* edx, unsigned int groupIndex,
                                                float* a, float* b, unsigned int* out)
        {
            __try
            {
                return g_origPortalVis(model, edx, groupIndex, a, b, out);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                static int s_portalLog = 0;
                if (s_portalLog < 10)
                {
                    ++s_portalLog;
                    WLOG_INFO("WMO portal-vis caught (groupIndex=%u)", groupIndex);
                }
                return 0;
            }
        }

        // Group-resident accessor guard: the native accessor indexes the group array by group index with no
        // bounds check, so a wild index (from a corrupt portal ref) reads past the array and faults on the
        // per-entry deref. Gate the call when groupIndex is past the group count and return 0 (the "not
        // resident" result every caller already null-checks); otherwise run the original.
        unsigned int __fastcall GroupResidentDetour(void* model, void* edx, unsigned int groupIndex,
                                                    unsigned int force)
        {
            const unsigned int groupCount =
                *reinterpret_cast<unsigned int*>(static_cast<char*>(model) + off::kOffGroupCount);
            if (groupIndex >= groupCount)
            {
                static int s_oobLog = 0;
                if (s_oobLog < 10)
                {
                    ++s_oobLog;
                    WLOG_INFO("WMO group-idx OOB gated (idx=%u count=%u)", groupIndex, groupCount);
                }
                return 0; // not resident
            }
            return g_origGroupResident(model, edx, groupIndex, force);
        }
    }

    void Install()
    {
        hook::Install("Wmo::ResolveMaterialTexture", off::kResolveMaterialTexture,
                      &MatResolveDetour, reinterpret_cast<void**>(&g_origMatResolve));
        hook::Install("Wmo::PortalVisibility", off::kPortalVisibility,
                      &PortalVisDetour, reinterpret_cast<void**>(&g_origPortalVis));
        hook::Install("Wmo::GroupResidentAccessor", off::kGroupResidentAccessor,
                      &GroupResidentDetour, reinterpret_cast<void**>(&g_origGroupResident));
        hook::Install("Wmo::GroupBatchDraw", off::kGroupBatchDraw,
                      &BatchDrawGuard, reinterpret_cast<void**>(&g_origBatchDraw));
        WLOG_INFO("WMO: runtime guards installed");
    }
}
