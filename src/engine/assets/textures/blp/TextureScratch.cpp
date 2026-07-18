// Widens the client's boot-time texture mip scratch so served textures up to 2048 load safely.
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

#include "config.hpp"
#include "common/Log.hpp"
#include "common/Mem.hpp"
#include "offsets/engine/Gx.hpp"
#include "runtime/ModuleInstall.hpp"

#include <cstdint>

// This module's host face serves tileset diffuse textures at up to 2048 px, but the client sizes its
// mip decode scratch at boot for 1024 px chains; a wider chain overflows it. The two size operands are
// patched through the boot-installer seam (DllMain, before any client boot code), so the allocation is
// made wide. Each site is verified against the stock operand first; an unexpected value leaves the
// client untouched.
namespace wxl::modern::assets::textures::blp
{
    namespace
    {
        namespace gxoff = wxl::offsets::engine::gx;

        void WidenMipScratch()
        {
            const uintptr_t sites[2] = { gxoff::kMipScratchDimHImm, gxoff::kMipScratchDimWImm };
            for (uintptr_t site : sites)
            {
                const uint32_t current = *reinterpret_cast<const uint32_t*>(site);
                if (current != gxoff::kMipScratchStockEdge)
                {
                    WLOG_INFO("texture: mip-scratch operand at %08X is %08X, expected %08X - not patched",
                              uint32_t(site), current, gxoff::kMipScratchStockEdge);
                    return;
                }
            }
            for (uintptr_t site : sites)
                wxl::mem::Patch(reinterpret_cast<void*>(site), &gxoff::kMipScratchWideEdge,
                                      sizeof(uint32_t));
            WLOG_INFO("texture: mip scratch widened to %u (2048-wide chains fit)",
                      gxoff::kMipScratchWideEdge);
        }

        // File-scope registration: the patch must precede the client's boot-time scratch allocation, so
        // it rides the boot-installer seam (DllMain) rather than the deferred module installer.
        struct Registrar
        {
            Registrar()
            {
                // Install the mip-scratch widen only when the modern-BLP format is compiled in; disabled, the registrar is inert.
                if constexpr (wxl::features::kModernBlp)
                    wxl::runtime::modules::RegisterBoot("wxl-modern-assets blp scratch", &WidenMipScratch);
            }
        } g_registrar;
    }
}
