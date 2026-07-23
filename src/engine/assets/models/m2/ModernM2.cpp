// m2: module bring-up + binding the core events to the M2 themes.
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

#include "ModernM2.hpp"

#include "config.hpp"
#include "common/Log.hpp"
#include "game/m2/M2.hpp"
#include "engine/assets/m2/M2Format.hpp"

#include "engine/assets/shared/models/m2/Contract.hpp"
#include "engine/assets/shared/models/m2/Particles.hpp"
#include "engine/assets/shared/models/m2/Ribbons.hpp"
#include "../../common/BoneBudget.hpp"
#include "Skin.hpp"

#include <windows.h>

#include <string_view>

namespace wxl::modern::assets::m2
{
    namespace ev  = wxl::events;
    namespace m2  = wxl::game::m2;
    namespace fmt = wxl::structure::m2;
    namespace bn  = wxl::modern::assets::common::bones;

    /**
     * @brief Binds the module's handlers to the core M2 events.
     */
    ModernM2::ModernM2()
    {
        if constexpr (wxl::features::kNativeM2)
        {
            on<&ModernM2::OnModelLoadPre>(ev::Event::OnModelLoadPre);
            on<&ModernM2::OnSkinFinalize>(ev::Event::OnM2SkinFinalize);
            on<&ModernM2::OnSetupBatchAlpha>(ev::Event::OnM2SetupBatchAlpha);
            on<&ModernM2::OnRibbonDraw>(ev::Event::OnRibbonDraw);

            WLOG_INFO("modern-assets: m2 live-engine half loaded");
        }
    }

    /**
     * @brief Drops any registration left on this model pointer before the native reader fills it.
     *
     * The engine reuses model addresses: a pointer freed by one model can be handed straight back for the
     * next. Clearing here means the registry only ever holds live models, and the native reader
     * (RegisterNativeLoaded) is the one place that puts one back in.
     * @param a Model load arguments carrying the model pointer.
     */
    void ModernM2::OnModelLoadPre(const ev::ModelLoadArgs& a)
    {
        registry_.Forget(a.model);
    }

    /**
     * @brief Splits any over-budget submesh, then rebuilds the material / texunit contract for the models
     *        the native MD21 reader filled.
     *
     * The bone-budget split (BoneBudget.hpp) is a hard client-engine constraint, not a format concern, so it
     * runs for every model whatever its origin. The shaderId decode + textureUnitLookup synth after it is
     * scoped to registered models only, because it assumes the modern packed shaderId encoding that the
     * native reader leaves on the live skin -- a stock v264 model already carries a resolved contract and
     * only needs the structural repoint when a split happened.
     * @param a Skin finalize arguments carrying the model pointer.
     */
    void ModernM2::OnSkinFinalize(const ev::M2SkinFinalizeArgs& a)
    {
        auto* md = m2::Header(a.model);
        auto* sk = m2::Skin(a.model);
        if (!md || !sk) return;

        std::vector<bn::SplitSection> sections;
        std::vector<bn::SplitRun> splitMap;
        uint32_t splitCount = 0;
        const char* pathStem = m2::PathStem(a.model);
        const bool split = bn::SplitSubmeshes(md, sk, sections, splitMap, splitCount,
                                              pathStem ? pathStem : "") && splitCount > 0;
        if (split)
            WLOG_INFO("modern-assets: bone-splitter produced %u extra sub-draw(s)", splitCount);

        if (registry_.Contains(a.model))
            skin::Rebuild(md, sk, splitMap, pathStem ? pathStem : "");
        else if (split)
            bn::RepointBatchesAfterSplit(sk, splitMap);
    }

    /**
     * @brief Delegates the draw-time alpha-key fixup to the particles theme, flagged for reshaped models.
     * @param a Batch-alpha setup arguments.
     */
    void ModernM2::OnSetupBatchAlpha(const ev::M2SetupBatchAlphaArgs& a)
    {
        // Alpha-key batches are a small minority of the scene; test the blend mode before paying
        // the registry lookup, which otherwise costs a shared-lock + hash find on EVERY batch of
        // every visible model.
        if (a.blendMode != particles::kBlendAlphaKey) return;
        particles::OnSetupBatchAlpha(a, registry_.Contains(a.model));
    }

    /**
     * @brief Delegates the ribbon draw fixup to the ribbons theme.
     * @param a Ribbon draw arguments.
     */
    void ModernM2::OnRibbonDraw(const ev::RibbonDrawArgs& a)
    {
        ribbons::OnRibbonDraw(a);
    }

    // File-scope instance self-registers its handlers at DLL load via the EventScript ctor.
    ModernM2 g_modernM2;

    /**
     * @brief Registers a model the native MD21 reader direct-filled into this module's registry
     *        (kFlagHotReshaped: the packed modern shaderIds are present on the live skin, so the
     *        finalize-time contract rebuild and the draw fixups must scope to it).
     * @param model Runtime model pointer.
     */
    void RegisterNativeLoaded(void* model)
    {
        if constexpr (wxl::features::kNativeM2)
            g_modernM2.registry_.Remember(model, common::AssetRegistry::kFlagHotReshaped);
        else
            (void)model;
    }

    /**
     * @brief Drops a native-reader registration (failed fill after registration).
     * @param model Runtime model pointer.
     */
    void ForgetNativeLoaded(void* model)
    {
        if constexpr (wxl::features::kNativeM2)
            g_modernM2.registry_.Forget(model);
        else
            (void)model;
    }
}
