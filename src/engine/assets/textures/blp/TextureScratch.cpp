// Widens the client's boot-time texture mip scratch and async read budget so served textures up to
// 2048 load safely and stream like any other.
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
#include "engine/hook/Registry.hpp"
#include "offsets/engine/Gx.hpp"

#include <cstdint>

// wxl-modern-blp can serve tileset diffuse textures at up to 2048 px, but the client sizes its
// mip decode scratch at boot for 1024 px chains; a wider chain overflows it. The two size operands are
// patched through the Boot-phase installer seam (DllMain, before any client boot code), so the
// allocation is made wide. Each site is verified against the stock operand first; an unexpected value
// leaves the client untouched.
//
// The async read budget is the streaming half of the same limit: a texture file larger than the
// client's 4 MB in-flight budget is never admitted, so a large texture requested in the world waits
// until a loading screen or /reloadui forces it, and its model stays undrawn until then. Both operands
// change together or not at all: one widened site alone would still park a large read forever.
namespace wxl::modern::assets::textures::blp
{
    namespace
    {
        namespace gxoff = wxl::offsets::engine::gx;

        bool WidenMipScratch()
        {
            const uintptr_t sites[2] = { gxoff::kMipScratchDimHImm, gxoff::kMipScratchDimWImm };
            for (uintptr_t site : sites)
            {
                const uint32_t current = *reinterpret_cast<const uint32_t*>(site);
                if (current != gxoff::kMipScratchStockEdge)
                {
                    WLOG_INFO("texture: mip-scratch operand at %08X is %08X, expected %08X - not patched",
                              uint32_t(site), current, gxoff::kMipScratchStockEdge);
                    return true;
                }
            }
            for (uintptr_t site : sites)
                wxl::mem::Patch(reinterpret_cast<void*>(site), &gxoff::kMipScratchWideEdge,
                                      sizeof(uint32_t));
            WLOG_INFO("texture: mip scratch widened to %u (2048-wide chains fit)",
                      gxoff::kMipScratchWideEdge);
            return true;
        }

        bool WidenReadBudget()
        {
            const uintptr_t sites[2] = { gxoff::kTexReadBudgetRequestImm, gxoff::kTexReadBudgetPumpImm };
            for (uintptr_t site : sites)
            {
                const uint32_t current = *reinterpret_cast<const uint32_t*>(site);
                if (current != gxoff::kTexReadBudgetStock)
                {
                    WLOG_INFO("texture: read-budget operand at %08X is %08X, expected %08X - not patched",
                              uint32_t(site), current, gxoff::kTexReadBudgetStock);
                    return true;
                }
            }
            if (!wxl::mem::Patch(reinterpret_cast<void*>(sites[0]), &gxoff::kTexReadBudgetWide, sizeof(uint32_t)))
            {
                WLOG_INFO("texture: read-budget operand at %08X could not be written - not patched",
                          uint32_t(sites[0]));
                return true;
            }
            if (!wxl::mem::Patch(reinterpret_cast<void*>(sites[1]), &gxoff::kTexReadBudgetWide, sizeof(uint32_t)))
            {
                const bool restored = wxl::mem::Patch(reinterpret_cast<void*>(sites[0]),
                                                      &gxoff::kTexReadBudgetStock, sizeof(uint32_t));
                WLOG_INFO("texture: read-budget operand at %08X could not be written - %s",
                          uint32_t(sites[1]), restored ? "stock budget restored" : "first operand left widened");
                return true;
            }
            WLOG_INFO("texture: async read budget widened to %u MB (large texture files stream)",
                      gxoff::kTexReadBudgetWide >> 20);
            return true;
        }
    }
}

WXL_REGISTER_FEATURE_PHASED("wxl-modern-blp mip scratch", true,
                            wxl::modern::assets::textures::blp::WidenMipScratch,
                            wxl::hook::Phase::Boot)
WXL_REGISTER_FEATURE_PHASED("wxl-modern-blp read budget", true,
                            wxl::modern::assets::textures::blp::WidenReadBudget,
                            wxl::hook::Phase::Boot)
