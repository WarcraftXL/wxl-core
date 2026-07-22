// M2 loader compatibility: model init/skin-finalize events plus a material-key guard for modern shaders.
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
#include "features/diag/AssetProfile.hpp"
#include "features/diag/DrawStats.hpp"
#include "features/m2native/M2Native.hpp"

#include "common/Log.hpp"
#include "game/m2/M2.hpp"
#include "offsets/game/M2.hpp"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace
{
    namespace ev    = wxl::events;
    namespace m2    = wxl::offsets::game::m2;
    namespace aprof = wxl::runtime::assetprof;

    m2::M2_InitFn               g_origM2Init             = nullptr;
    m2::M2_FinalizeSkinFn       g_origFinalizeSkin       = nullptr;
    m2::M2_BuildBatchMaterialFn g_origBuildBatchMaterial = nullptr;

    /**
     * @brief Detours model init, emitting OnModelLoadPre at entry and OnModelLoad after parsing.
     *
     * When the native MD21 reader is compiled in (config.hpp kNativeM2) and the resident buffer is
     * a modern chunked container, the stock parser is NOT called: features/m2native direct-fills
     * the CM2Shared runtime from the modern body and returns the parser's own result contract.
     * A stock MD20 model pays one magic compare and takes the untouched original.
     * @param model  runtime model receiving the parsed file (raw bytes at model+0x150, size at +0x16c).
     * @return the original model-init result (or the native fill's, for an MD21 container).
     */
    int __fastcall hkM2Init(void* model)
    {
        const uint64_t preStarted = aprof::Now();
        ev::ModelLoadArgs pre{ model };
        ev::Emit(ev::Event::OnModelLoadPre, &pre);
        if (preStarted) aprof::Record(aprof::Phase::M2Pre, aprof::Now() - preStarted);

        const uint64_t nativeStarted = aprof::Now();
        int r;
        if (wxl::features::kNativeM2 && wxl::runtime::m2native::IsModernContainer(model))
            r = wxl::runtime::m2native::NativeLoad(model);
        else
            r = g_origM2Init(model);
        if (nativeStarted) aprof::Record(aprof::Phase::M2Native, aprof::Now() - nativeStarted);

        const uint64_t postStarted = aprof::Now();
        ev::ModelLoadArgs a{ model };
        ev::Emit(ev::Event::OnModelLoad, &a);
        if (postStarted) aprof::Record(aprof::Phase::M2Post, aprof::Now() - postStarted);
        return r;
    }

    bool ProbeEffectObject(void* effect) noexcept
    {
        if (!effect) return true;
        __try
        {
            volatile uint8_t head = *static_cast<uint8_t*>(effect);
            volatile uint32_t vertexTable = *reinterpret_cast<uint32_t*>(
                static_cast<uint8_t*>(effect) + 0x2C);
            volatile uint32_t pixelTable = *reinterpret_cast<uint32_t*>(
                static_cast<uint8_t*>(effect) + 0x194);
            (void)head;
            (void)vertexTable;
            (void)pixelTable;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    /**
     * @brief Detours skin finalize, emitting OnM2SkinFinalize before the native sizing runs.
     *
     * The skin profile is attached, pointer-fixed and its header live before the native finalize
     * sizes its parallel batch blocks from skin->batchCount, so a subscriber can rebuild a
     * material/texunit contract a modern skin omits.
     * @param model  model whose skin is being finalized.
     */
    void __fastcall hkFinalizeSkin(void* model)
    {
        ev::M2SkinFinalizeArgs a{ model };
        ev::Emit(ev::Event::OnM2SkinFinalize, &a);
        g_origFinalizeSkin(model);

        // Native finalize has just computed "max instances per batched draw" at shared+0x194. Record it:
        // CM2Model::InitializeLoaded clears the batchable-doodad flag 0x10 when it is < 2, so this is the
        // one place that can tell us how much of the corpus opts out of instanced doodad batching.
        if constexpr (wxl::features::kDiag)
        {
            __try
            {
                wxl::runtime::drawstats::RecordFinalize(
                    *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(model) +
                                                       m2::kOffSharedMaxInstances));
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // Native finalize stores one optional CShaderEffect pointer per skin batch at model+0x188.
        // Diagnose and clear values that are already invalid here; the sorter guard (Batches.cpp) covers
        // keys that become stale later (for example across an effect-manager/device lifecycle transition).
        uint32_t invalid = 0;
        uint32_t firstIndex = 0;
        void* firstEffect = nullptr;
        char path[128]{};
        __try
        {
            auto* bytes = static_cast<uint8_t*>(model);
            auto* skin = *reinterpret_cast<wxl::game::m2::M2SkinProfile**>(bytes + m2::kOffModelSkin);
            auto** effects = *reinterpret_cast<void***>(bytes + m2::kOffModelSubMeshCopy);
            if (skin && effects)
            {
                const uint32_t count = std::min<uint32_t>(skin->batchCount, 0x4000u);
                for (uint32_t i = 0; i < count; ++i)
                {
                    void* effect = effects[i];
                    if (effect && !ProbeEffectObject(effect))
                    {
                        if (invalid == 0) { firstIndex = i; firstEffect = effect; }
                        effects[i] = nullptr;
                        ++invalid;
                    }
                }
            }

            const char* source = reinterpret_cast<const char*>(bytes + m2::kOffModelPathStem);
            size_t i = 0;
            for (; i + 1 < sizeof path && source[i]; ++i) path[i] = source[i];
            path[i] = '\0';
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            path[0] = '\0';
        }
        if (invalid)
            WLOG_WARN("M2 finalize: cleared %u invalid effect key(s), first batch=%u effect=%p model='%s'",
                      invalid, firstIndex, firstEffect, path[0] ? path : "(unreadable)");
    }

    /**
     * @brief Guards the per-batch material-key builder against unimplemented shader types.
     *
     * kBuildBatchMaterial reads M2Batch::shaderId (batch+2) and uses bits 0-14 as a
     * switch index when bit 0x8000 is set. The switch only handles values 0-3; for higher values it
     * falls through with EBX=0 and crashes at 0x836D11 (mov cl, [eax] with eax=0).
     * Modern collection M2s can have shaderId values > 3; returning nullptr for those is safe because
     * kFinalizeSkin only stores the result in the model+0x188 array, which the IB build path does not
     * dereference directly.
     * @param model    model object (ECX thiscall this pointer).
     * @param edx      unused register slot (thiscall via fastcall trampoline).
     * @param batchPtr pointer to the M2Batch entry from skin->batches.
     * @return the material-key object, or nullptr for unimplemented shader types.
     */
    void* __fastcall hkBuildBatchMaterial(void* model, void* /*edx*/, void* batchPtr)
    {
        if (batchPtr)
        {
            uint16_t shaderId = *reinterpret_cast<uint16_t*>(
                static_cast<uint8_t*>(batchPtr) + 2);
            if ((shaderId & 0x8000u) && (shaderId & 0x7FFFu) > 3u)
                return nullptr;
        }
        return g_origBuildBatchMaterial(model, nullptr, batchPtr);
    }

    bool InstallM2CompatLoader()
    {
        wxl::hook::Install("M2Init", m2::kInit, &hkM2Init, &g_origM2Init);
        wxl::hook::Install("M2FinalizeSkin", m2::kFinalizeSkin, &hkFinalizeSkin, &g_origFinalizeSkin);
        wxl::hook::Install("M2BuildBatchMaterial", m2::kBuildBatchMaterial,
                           &hkBuildBatchMaterial, &g_origBuildBatchMaterial);
        return true;
    }
}

WXL_REGISTER_FEATURE("m2compat-loader", wxl::features::kM2Compat, InstallM2CompatLoader)
