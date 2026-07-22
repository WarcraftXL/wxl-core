// Compile-time feature toggles: one constexpr bool per feature folder under features/.
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

#pragma once

/**
 * @brief Build-time presence switches, one per feature.
 *
 * Each flag decides whether a feature EXISTS in the binary: its registrar is compiled in and its
 * installer runs only when the flag is true, so a disabled feature installs no hook and its code
 * is eliminable by the linker. This is the compile-time layer; runtime tuning of a feature that
 * IS present lives in WarcraftXL.cfg (common/Config). Anyone cloning the project trims the build
 * here and reads the register log to confirm what shipped.
 */
namespace wxl::features
{
    inline constexpr bool kStreaming    = true; // async file read, drain, chunk build
    inline constexpr bool kM2Memory     = true; // dedicated M2 buffer arena
    inline constexpr bool kM2Compat     = true; // M2 version gates, batch/skin/bone/shadow fixes, guards
    // Ground-shadow space fix: the 3.3.5 shadow draw skins its geometry with the VIEW-space bone palette
    // the main mesh left in c31 while pushing a WORLD-space root, so any bone with a non-identity
    // model-space transform casts a shadow that rotates with the camera. When true, the shadow-batch draw
    // (0x00829AA0) is detoured to rebase c31 into model space for the duration of the draw and restore it
    // after. This fixes the CLIENT and touches no asset bytes: the model's boneCombos table stays intact.
    // Live A/B toggle: wxl.m2.set_shadow_fix_enabled(false) restores stock behaviour without a rebuild.
    inline constexpr bool kM2ShadowSpaceFix = kM2Compat && true;
    inline constexpr bool kSpawn        = true; // doodad/WMO spawn and parse callbacks
    inline constexpr bool kTextures     = true; // texture create/update
    inline constexpr bool kWorld        = true; // world enter, frame pump, liquid-row guard
    inline constexpr bool kUnit         = true; // object update/destroy, target set
    inline constexpr bool kSound        = true; // play sound / sound kit
    inline constexpr bool kCharModel    = true; // equipment slot dispatch
    inline constexpr bool kInput        = true; // window subclass republishing messages as OnInput
    inline constexpr bool kRender       = true; // native D3D9 render hooks (device vtable, GX)
    inline constexpr bool kPhasing      = true; // terrain-phase per-tile loader redirect
    inline constexpr bool kLuaBindings  = true; // register functions into the client UI VM
    inline constexpr bool kOverlay      = true; // ImGui dev overlay (dormant)
    inline constexpr bool kModernAssets = true; // master switch for the modern (retail) asset pipelines
    // Per-format modern-asset toggles (ex CMake WXL_MODERN_* options, now honored in-code). Each pipeline's
    // registrar (client EventScript / host Transform) checks its flag with `if constexpr` and self-registers
    // only when true; disabled code is eliminable. The master ANDs in, so kModernAssets=false kills them all.
    inline constexpr bool kModernM2  = kModernAssets && true; // M2 (MD21) reshape onto client version 264
    // Native in-client modern-M2 reader: the 3.3.5 client READS a Legion-window MD21 container
    // (inner version 272-274) directly at the model-init detour and direct-fills the stock
    // CM2Shared runtime -- NO conversion anywhere (no host transform, no in-memory MD21->MD20
    // reshape; the resident header keeps its modern version). Stock MD20 models take the untouched
    // original parser (one magic compare). Requires kModernM2: the live-engine half (skin-finalize
    // contract rebuild, bone-budget split, draw fixups) is what makes the filled model render.
    // A/B against the legacy host transform via the WXL_MODERN_M2_HOST env kill-switch (host side,
    // default OFF now that the native reader handles the corpus).
    inline constexpr bool kNativeM2  = kModernM2 && true;
    // Modern particle emitters, read where they lie. The record grew 0x1DC -> 0x1EC but every field
    // below 0x1DC kept its offset, so the client only has to STEP differently: the nine instructions
    // that hardcode the stride are redirected at thunks deriving it from the model being processed
    // (correct for scenes mixing stock v264 and modern doodads). Also neutralizes the modern
    // zSource = 255.0 "no point source" sentinel, which the client would otherwise read as a real
    // point source -- the reason modern fire drifts sideways instead of rising. Nothing is repacked
    // or copied. Off => emitters stay parked and modern models show no fire/smoke at all.
    inline constexpr bool kNativeM2Particles = kNativeM2 && true;
    inline constexpr bool kModernM3  = kModernAssets && true; // M3 (MD34) bake to client M2 + skin
    inline constexpr bool kModernWmo = kModernAssets && true; // WMO root/group down-convert
    inline constexpr bool kModernBlp = kModernAssets && true; // BLP/texture transcode + mip scratch widen
    inline constexpr bool kDiag         = true; // asset-load and frame-hitch diagnostics
    inline constexpr bool kGrassWind    = true; // detail-doodad (grass) wind sway: native shear injection
    // Native Cata+ split-ADT tile reader (root + _tex0 + _obj0 -> direct-fill of the stock CMapChunk).
    // Safe default: split-ness is probed once per map and cached; a non-split (stock 3.3.5) map takes
    // the untouched native path -- the detours add only a cached-map lookup per tile load.
    inline constexpr bool kAdtSplit     = true;
    // Placed-object control, split by kind (a Legion+ map references both by FileDataID).
    //  - WMOs (MODF): dropped until native WMO reading lands -- a MODF FileDataID hits CMap::SafeOpen
    //    which fatals (#134) trying to open it by (reversed) name. Keep true until WMO resolves.
    //  - Doodads (MDDF): KEPT. Their FileDataID nameIds (entry flag 0x40) are resolved via
    //    ModelFilePath into a real synthesized MMDX/MMID name table (see AdtSplit ParseAndPatch), so
    //    the stock placement path hands the native M2 reader a valid model name. false => doodads show.
    inline constexpr bool kAdtSplitSkipWmo     = true;
    inline constexpr bool kAdtSplitSkipDoodads = false;
    // Deprecated alias (kept so no published toggle is silently deleted): "skip ALL placed objects".
    inline constexpr bool kAdtSplitSkipObjects = kAdtSplitSkipWmo && kAdtSplitSkipDoodads;
    // Resolve split-tile terrain textures by FileDataID and honor the Cata+ 8-bit (4096) alpha maps.
    // A Legion+ _tex0 has no MTEX name table (textures are MDID/MHID FileDataIDs) and ships big, often
    // RLE-compressed alpha. When true, TLK's tile texture manager (CMapArea::LoadTextures /
    // CMap::LoadTerrainTexture) is patched to source names from the real MDID and resolve each at load
    // time, and the MPHD big-alpha bit is asserted per split chunk so the stock unpacker reads 4096
    // maps as 4096. The ADT is never rewritten; we adapt TLK. Off => untextured (green) split tiles.
    inline constexpr bool kAdtSplitTextures = true;
    // Legion-style height-based terrain texture blending for split tiles (MTXP/MHID "_h" textures).
    // The per-chunk terrain draw is detoured: for a chunk of a height-textured (WDT MPHD 0x80) split
    // tile it binds the 4 layer height textures at s9..s12, uploads heightScale/heightOffset (+ a
    // runtime sharpness tunable) at c22..c24, and swaps the active terrain pixel shader for a
    // runtime-patched copy computing the retail weight formula. Non-height maps take the untouched
    // stock draw (one flag check). Runtime knobs live in WarcraftXL.cfg (WXL_ADT_HEIGHT_*).
    inline constexpr bool kAdtSplitHeightBlend = true;
    // INTERIM: drop the tile's MH2O water. A Legion+ MH2O references liquid types by ids beyond the
    // 3.3.5 LiquidType.dbc range; the stock liquid-sound query looks the id up, clamps an out-of-range
    // one to a null record, then dereferences it unconditionally (null+8) -> #132 the moment the player
    // nears water. Dropping MH2O (MHDR liquid offset zeroed) removes the water and the crash so the map
    // stays explorable. Flip false once native MH2O + liquid-type remap lands.
    inline constexpr bool kAdtSplitSkipLiquid = true;
}
