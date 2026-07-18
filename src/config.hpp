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
    inline constexpr bool kModernAssets = true; // client integration of modern (retail) assets
    inline constexpr bool kDiag         = true; // asset-load and frame-hitch diagnostics
}
