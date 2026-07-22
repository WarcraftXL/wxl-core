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

#include "engine/assets/shared/common/WxlTag.hpp"
#include "engine/assets/shared/models/m2/Contract.hpp"
#include "engine/assets/shared/models/m2/Downport.hpp"
#include "engine/assets/shared/models/m2/Particles.hpp"
#include "engine/assets/shared/models/m2/Ribbons.hpp"
#include "engine/assets/shared/models/m2/Textures.hpp"
#include "../../common/BoneBudget.hpp"
#include "Skin.hpp"

#include <windows.h>

#include <cstring>
#include <string_view>

namespace wxl::modern::assets::m2
{
    namespace ev  = wxl::events;
    namespace m2  = wxl::game::m2;
    namespace fmt = wxl::structure::m2;
    namespace bn  = wxl::modern::assets::common::bones;
    namespace tex = wxl::modern::assets::m2::textures;

    namespace
    {
        bool SkinRebuildEnabled()
        {
            static const bool enabled = [] {
                char env[16]{};
                const DWORD n = GetEnvironmentVariableA("WXL_MODERN_M2_SKIN_REBUILD", env, sizeof env);
                if (n > 0 && (env[0] == '0' || env[0] == 'n' || env[0] == 'N')) return false;
                return GetFileAttributesA("WarcraftXL_modern_m2_skin_rebuild.disable") ==
                       INVALID_FILE_ATTRIBUTES;
            }();
            return enabled;
        }

        void RemapWeaponBladeTextures(void* model, fmt::M2Header* header, uint32_t size)
        {
            const char* path = model ? m2::PathStem(model) : nullptr;
            if (!header || !path || !tex::AllowsWeaponBladeRemap(path)) return;
            const auto patched = tex::FixWeaponBladeTextureTypes(header, size);
            if (patched.Total())
                WLOG_INFO("modern-assets: %s fixed WEAPON_BLADE textures objectSkin=%u hardcoded=%u",
                          path, patched.toObjectSkin, patched.toHardcoded);
        }
    }

    /**
     * @brief Binds the module's handlers to the core M2 events.
     */
    ModernM2::ModernM2()
    {
        // Bind the M2 handlers only when the modern-M2 format is compiled in; disabled, the instance is inert.
        if constexpr (wxl::features::kModernM2)
        {
            on<&ModernM2::OnModelLoadPre>(ev::Event::OnModelLoadPre);
            on<&ModernM2::OnModelLoad>(ev::Event::OnModelLoad);
            on<&ModernM2::OnSkinFinalize>(ev::Event::OnM2SkinFinalize);
            on<&ModernM2::OnSetupBatchAlpha>(ev::Event::OnM2SetupBatchAlpha);
            on<&ModernM2::OnRibbonDraw>(ev::Event::OnRibbonDraw);

            WLOG_INFO("modern-assets: m2 loaded (in-memory modern M2 asset support)");
        }
    }

    /**
     * @brief Reshapes the .m2 bytes in the model's load buffer onto the native version before the parser runs,
     *        and registers the model for the live-engine half.
     *
     * The byte-transform may have already run on the host (the image arrives compacted with its inner version
     * staged) or it runs here in process. Either way the model is finalized to the native version and
     * registered for the skin rebuild at finalize and the draw-time fixups.
     * @param a Model load arguments carrying the model pointer and load buffer.
     */
    void ModernM2::OnModelLoadPre(const ev::ModelLoadArgs& a)
    {
        // A fresh model now occupies this pointer; drop any registration left from a prior model freed at
        // the same address (only a successful path below re-registers it).
        registry_.Forget(a.model);

        void* buf = m2::FileBuffer(a.model);
        const uint32_t size = m2::FileSize(a.model);
        if (!buf || size < sizeof(fmt::M2Header)) return;
        auto* md = static_cast<fmt::M2Header*>(buf);
        if (md->magic != fmt::kMagicMD20) return;

        // Host already compacted it: the records are on the client contract and the version is staged. Clear
        // the staging bit to hand the parser the clean native version, then register it.
        if (IsStagedVersion(md->version))
        {
            RemapWeaponBladeTextures(a.model, md, size);
            md->version &= ~kStagedVersionBit;
            registry_.Remember(a.model, common::AssetRegistry::kFlagHotReshaped);
            return;
        }

        // Pre-converted file (cold converter output): already on the client contract, discriminated from
        // truly native content only by its WXLC trailer. Register it with the tag's flags so the draw-time
        // fixups the converter cannot bake (device state like the alpha-key ref) still scope to it -- the
        // skin rebuild does NOT run for these (the converter already resolved the material contract).
        uint32_t tagFlags = 0;
        if (wxl::modern::assets::common::tag::Read(static_cast<const uint8_t*>(buf), size, tagFlags) &&
            tagFlags != 0)
        {
            registry_.Remember(a.model, tagFlags & ~common::AssetRegistry::kFlagHotReshaped);
            return;
        }

        // Host off (or a file the host did not serve): reshape in process. Downport never allocates; the
        // caller owns the buffer. Reshape within the load buffer when the image does not grow; otherwise
        // allocate a replacement with the model allocator (so the destructor frees it), copy, reshape, swap.
        if (!downport::IsConvertible(buf, size)) return;
        downport::Inspect(buf, size);

        const char* pathStem = m2::PathStem(a.model);
        const std::string_view path = pathStem ? pathStem : "";
        const uint32_t workSize = downport::WorkSize(buf, size, path);
        void* image = buf;
        if (workSize == size)
        {
            if (!downport::ProcessInPlace(buf, size, size, path)) return;
        }
        else
        {
            void* out = m2::AllocBuffer(workSize, ".\\wxl-modern-assets");
            if (!out) return;
            std::memcpy(out, buf, size);
            if (!downport::ProcessInPlace(out, size, workSize, path)) { m2::FreeBuffer(out); return; }
            m2::ReplaceBuffer(a.model, out, workSize); // parser now reads the grown image
            m2::FreeBuffer(buf);                        // release the original source bytes
            image = out;
        }

        // ProcessInPlace staged the version; finalize it to the native value so the parser accepts it, then
        // register the model (no longer distinguishable by version at draw time) for the live-engine half.
        auto* imageHeader = static_cast<fmt::M2Header*>(image);
        RemapWeaponBladeTextures(a.model, imageHeader, workSize);
        imageHeader->version &= ~kStagedVersionBit;
        registry_.Remember(a.model, common::AssetRegistry::kFlagHotReshaped);
        WLOG_INFO("modern-m2: reshaped M2 to 264 (%u -> %u bytes)", size, workSize);
    }

    /**
     * @brief Parsed-model load hook.
     * @param a Model load arguments.
     */
    void ModernM2::OnModelLoad(const ev::ModelLoadArgs& a)
    {
        (void)a; // parsed-model hook for fixups that need the parsed object
    }

    /**
     * @brief Splits any over-budget submesh unconditionally, then rebuilds the material / texunit
     *        contract for models this module reshaped.
     *
     * The bone-budget split (BoneBudget.hpp) is a hard client-engine constraint, not a modern-M2-source
     * concern, so it runs for every model regardless of origin (native, downported, or a client M2 another
     * format baked). Only the shaderId decode + textureUnitLookup synth after it is scoped to models this
     * module reshaped, since that step assumes the source's packed shaderId encoding.
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

        // The rebuild assumes the source's packed shaderId encoding, which only an in-memory reshape leaves
        // behind -- a cold-converted (tagged) model already carries the resolved contract and must not be
        // rebuilt, only structurally repointed like native content when a split occurred.
        const bool hotReshaped =
            (registry_.Flags(a.model) & common::AssetRegistry::kFlagHotReshaped) != 0;
        if (hotReshaped && SkinRebuildEnabled())
            skin::Rebuild(md, sk, splitMap, pathStem ? pathStem : ""); // full modern-M2 contract rebuild
        else if (split)
            bn::RepointBatchesAfterSplit(sk, splitMap); // native / other-origin content: structural repoint only
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
        if constexpr (wxl::features::kModernM2)
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
        if constexpr (wxl::features::kModernM2)
            g_modernM2.registry_.Forget(model);
        else
            (void)model;
    }
}
