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
 * installer runs only when the flag is true, so a disabled feature installs no hook and its code is
 * eliminable by the linker. This is the compile-time layer; runtime tuning of a feature that IS
 * present lives in WarcraftXL.cfg (common/Config) or behind a wxl.* Lua setter.
 *
 * A switch belongs here only when both states are real. What a feature does, why it exists and what
 * was measured to justify it belongs in docs/TODO.md, not in a comment here.
 */
namespace wxl::features
{
    // --- client subsystems -------------------------------------------------------------------
    inline constexpr bool kStreaming   = true; // async file read, drain, chunk build
    inline constexpr bool kM2Memory    = true; // dedicated M2 buffer arena
    inline constexpr bool kM2Compat    = true; // M2 version gates, batch/skin/bone/shadow fixes, guards
    inline constexpr bool kSpawn       = true; // doodad/WMO spawn and parse callbacks
    inline constexpr bool kTextures    = true; // texture create/update
    inline constexpr bool kWorld       = true; // world enter, frame pump, liquid-row guard
    inline constexpr bool kUnit        = true; // object update/destroy, target set
    inline constexpr bool kSound       = true; // play sound / sound kit
    inline constexpr bool kCharModel   = true; // equipment slot dispatch
    inline constexpr bool kInput       = true; // window subclass republishing messages as OnInput
    inline constexpr bool kRender      = true; // native D3D9 render hooks (device vtable, GX)
    inline constexpr bool kPhasing     = true; // terrain-phase per-tile loader redirect
    inline constexpr bool kLuaBindings = true; // register functions into the client UI VM
    inline constexpr bool kOverlay     = true; // ImGui dev overlay (dormant)
    inline constexpr bool kDiag        = true; // asset-load and frame-hitch diagnostics
    inline constexpr bool kGrassWind   = true; // detail-doodad (grass) wind sway: native shear injection

    // --- modern assets -----------------------------------------------------------------------
    // Master switch for reading retail (Legion+) assets on this client. Off => the client sees only
    // what its own 3.3.5 parsers understand.
    inline constexpr bool kModernAssets = true;

    inline constexpr bool kNativeM2  = kModernAssets && true; // MD21 read in place -> stock CM2Shared
    inline constexpr bool kNativeWmo = kModernAssets && true; // tag-driven root/group walkers, MOMT by FDID
    inline constexpr bool kAdtSplit  = kModernAssets && true; // Cata+ split tile (root + _tex0 + _obj0)
    inline constexpr bool kModernM3  = kModernAssets && true; // M3 (MD34) bake to client M2 + skin
    inline constexpr bool kModernBlp = kModernAssets && true; // BLP transcode + mip scratch widen
    inline constexpr bool kModernAnim = kModernAssets && true; // host-side AFM2 unwrap of external .anim

    // Legion-style height-based terrain blending for split tiles (MTXP/MHID "_h" textures). A map
    // without the WDT MPHD 0x80 bit takes the untouched stock draw either way; off means a height map
    // renders with abrupt layer cutoffs instead. Runtime knobs: WarcraftXL.cfg WXL_ADT_HEIGHT_*.
    inline constexpr bool kAdtSplitHeightBlend = kAdtSplit && true;

    // Legion's exterior re-enable rule added to the 3.3.5 indoor/outdoor gate, plus the antiportal
    // occluder handling. Live A/B: wxl.wmo.set_outdoor_rule_enabled / set_modern_occluders_disabled.
    inline constexpr bool kWmoOutdoorGate = kNativeWmo && true;

    // Modern-WMO two-layer fix: a modern material's texture_2 is an alpha-masked detail overlay, but the
    // stock Composite pixel shader blends it by the secondary vertex-colour alpha (rendering its dark
    // body as stripes). Patches the Composite PS IN MEMORY to composite by the texture's own alpha, for
    // modern WMOs only. Live A/B: wxl.wmo.set_composite_alpha_fix.
    inline constexpr bool kWmoCompositeAlphaFix = kNativeWmo && true;

    // --- open arbitrations -------------------------------------------------------------------
    // These are switches because the RIGHT answer is not settled yet, not because a fallback is kept
    // around. Each one has an entry in docs/TODO.md saying what would close it.

    // Copy a modern MOBA's material index (u16 @+0x0A) into the byte 3.3.5 reads (@+0x17). The only
    // place this feature moves a value instead of teaching the client to read; the conforming version
    // is a thunk on the 7 read sites. False shows what the client does with the record untouched.
    inline constexpr bool kNativeWmoMobaMaterialId = kNativeWmo && true;

    // Fold the per-instance MODF scale into the collision/portal basis too. That copy is the
    // TRANSPOSED basis, so it takes 1/s: inverse(R*s) == inverse(R)/s. An earlier attempt scaled it
    // by s and made interiors vanish, so the sign stays verifiable rather than assumed.
    inline constexpr bool kWmoScaleCollision = kSpawn && true;

    // Ground-shadow probe: counts (model, section) pairs on the shadow path. Not a fix -- the
    // camera-locked shadow swing is still open, and this is what refutes hypotheses about it.
    inline constexpr bool kM2ShadowSpaceFix = kM2Compat && true;

    // INTERIM: drop the tile's MH2O water. A Legion+ MH2O names liquid types beyond LiquidType.dbc,
    // and the client's liquid-sound query dereferences the resulting null record (#132). Flip false
    // once native MH2O + the liquid-type remap land.
    inline constexpr bool kAdtSplitSkipLiquid = kAdtSplit && true;
}
