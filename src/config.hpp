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
    inline constexpr bool kModernM3  = kModernAssets && true; // M3 (MD34) bake to client M2 + skin
    inline constexpr bool kModernWmo = kModernAssets && true; // WMO root/group down-convert
    inline constexpr bool kModernBlp = kModernAssets && true; // BLP/texture transcode + mip scratch widen
    inline constexpr bool kDiag         = true; // asset-load and frame-hitch diagnostics
    inline constexpr bool kGrassWind    = true; // detail-doodad (grass) wind sway: native shear injection
    // Native Cata+ split-ADT tile reader (root + _tex0 + _obj0 -> direct-fill of the stock CMapChunk).
    // Safe default: split-ness is probed once per map and cached; a non-split (stock 3.3.5) map takes
    // the untouched native path -- the detours add only a cached-map lookup per tile load.
    inline constexpr bool kAdtSplit     = true;
    // INTERIM (until FileDataID model resolution lands): split tiles drop ONLY their placed-object layer
    // (doodads MDDF + map objects MODF). A Legion+ custom map references those models by FileDataID,
    // which 3.3.5 cannot resolve -> CMap::SafeOpen fatals (#134). Dropping them lets the terrain (geometry
    // + texture layers) load and the map be walkable/TP-able. Texture layers are kept -- an unresolved
    // FDID texture renders green, never fatal. Flip false once models resolve.
    inline constexpr bool kAdtSplitSkipObjects = false;
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
