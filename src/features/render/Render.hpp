// Feature-internal wiring shared between the render feature's translation units.
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

/// Private surface between Render.cpp (device-vtable install + frame/reset detours) and M2Draw.cpp
/// (the M2 batch draw path). The DrawIndexedPrimitive detour lives in the M2 draw unit but is patched
/// into the device vtable by the installer, so its address and trampoline are shared here. Not a public
/// header: the feature self-registers and exposes nothing to the rest of the engine.
namespace wxl::features::render::detail
{
    /** @brief IDirect3DDevice9::DrawIndexedPrimitive signature (stdcall thunk taking the device as this). */
    using DIPFn = long (__stdcall*)(void*, int, int, unsigned, unsigned, unsigned, unsigned);

    /** @brief Trampoline to the native DrawIndexedPrimitive, set when the device vtable is patched. */
    extern DIPFn g_origDIP;

    /**
     * @brief DrawIndexedPrimitive detour: folds multi-texture ribbons, re-expands 32-bit M2 start indices,
     *        and publishes OnM2BatchDraw. Patched into the device vtable by the installer in Render.cpp.
     */
    long __stdcall hkDIP(void* dev, int pt, int bv, unsigned mi, unsigned nv, unsigned si, unsigned pc);

    /**
     * @brief Installs the M2 function-entry detours (batch draw model capture, ribbon-emitter draw).
     *        Called once by the feature installer; enabled by the caller's batch EnableAll().
     */
    void InstallM2DrawHooks();
}
