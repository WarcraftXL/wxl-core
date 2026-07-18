// M2 bone compatibility: post-fill bone-palette event and an SM3 constant-cache guard for shadow batches.
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
#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"
#include "engine/events/Event.hpp"

#include "common/Log.hpp"
#include "offsets/game/M2.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace
{
    namespace ev = wxl::events;
    namespace m2 = wxl::offsets::game::m2;

    m2::M2_BuildBonePaletteFn     g_origBuildBonePalette     = nullptr;
    m2::M2_RenderBatchShadowMapFn g_origRenderBatchShadowMap = nullptr;
    std::atomic<uint32_t>         g_shadowBoneOverflowSkips{ 0 };

    /**
     * @brief Detours bone-palette build, emitting OnBuildBonePalette after the engine fills the buffer.
     *
     * Called from two sites per collection M2 per frame:
     *   (a) the attached-model update path, inside kM2PerFrameUpdate of the parent character.
     *   (b) The outer scene-traversal loop (0x821B4E), which runs AFTER the parent's PerFrameUpdate.
     *
     * Site (b) overwrites any bone-palette modifications that OnM2PerFrameUpdate subscribers made,
     * reverting the collection M2 to its bind pose every frame. By hooking POST-order here,
     * subscribers can re-apply their modifications immediately after the engine's fill -- guaranteed
     * to be the last write before the GPU upload regardless of scene-list ordering.
     *
     * Calling convention: fastcall, ecx = renderCtx, 5 stack args, ret 0x14 (callee-cleanup).
     */
    void __fastcall hkBuildBonePalette(void* renderCtx, void* edx,
        void* sa1, void* sa2, void* sa3, uint32_t sa4, uint32_t sa5)
    {
        g_origBuildBonePalette(renderCtx, edx, sa1, sa2, sa3, sa4, sa5);
        // Per-instance-per-frame site: skip the emission entirely while nothing subscribes.
        if (ev::Any(ev::Event::OnBuildBonePalette))
        {
            ev::BuildBonePaletteArgs a{ renderCtx };
            ev::Emit(ev::Event::OnBuildBonePalette, &a);
        }
    }

    /**
     * @brief Rejects M2 shadow batches whose palette would overrun WoW's VS constant cache.
     *
     * The native shadow path begins at c31 and copies three float4 registers per bone. The cache contains
     * c0..c255, so 75 bones is the largest representable palette. Retail skins can carry larger batches when
     * a transform/split was skipped or failed; letting the native function process one overwrites the adjacent
     * Gx vertex-declaration table with bone-matrix floats and crashes later in GxPrimVertexPtr.
     */
    void __fastcall hkRenderBatchShadowMap(
        void* instance, void*, uint32_t batchMode, void* skinBatch, void* drawList,
        uint32_t drawIndex, void* skinSection, void* previousSection)
    {
        constexpr uint32_t kMaxShadowBones = (256u - 31u) / 3u;
        uint32_t boneCount = 0;
        __try
        {
            if (skinSection)
                boneCount = *reinterpret_cast<const uint16_t*>(
                    static_cast<const uint8_t*>(skinSection) + 0x0C);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            boneCount = kMaxShadowBones + 1;
        }

        if (boneCount > kMaxShadowBones)
        {
            const uint32_t skipped = ++g_shadowBoneOverflowSkips;
            if (skipped <= 32 || (skipped % 1000u) == 0)
            {
                char path[264] = "<unreadable>";
                __try
                {
                    const auto* bytes = static_cast<const uint8_t*>(instance);
                    const auto* model = bytes ? *reinterpret_cast<void* const*>(bytes + m2::kOffInstModel) : nullptr;
                    if (model)
                    {
                        std::strncpy(path,
                            reinterpret_cast<const char*>(model) + m2::kOffModelPathStem,
                            sizeof(path) - 1);
                        path[sizeof(path) - 1] = '\0';
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    std::strcpy(path, "<unreadable>");
                }
                WLOG_WARN("M2 shadow: skipped oversized palette bones=%u max=%u draw=%u model='%s' (skips=%u)",
                          boneCount, kMaxShadowBones, drawIndex, path, skipped);
            }
            return;
        }

        g_origRenderBatchShadowMap(instance, nullptr, batchMode, skinBatch, drawList,
                                   drawIndex, skinSection, previousSection);
    }

    bool InstallM2CompatBones()
    {
        wxl::hook::Install("M2BuildBonePalette", m2::kBuildBonePalette,
                           &hkBuildBonePalette, &g_origBuildBonePalette);
        wxl::hook::Install("M2RenderBatchShadowMap", m2::kRenderBatchShadowMap,
                           &hkRenderBatchShadowMap, &g_origRenderBatchShadowMap);
        return true;
    }
}

WXL_REGISTER_FEATURE("m2compat-bones", wxl::features::kM2Compat, InstallM2CompatBones)
