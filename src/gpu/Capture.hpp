// Captures the engine's D3D9 device by intercepting IDirect3D9::CreateDevice.
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

#include <d3d9.h>

/**
 * @brief Captures the engine's D3D9 device by intercepting IDirect3D9::CreateDevice on the main thread.
 */
namespace wxl::gpu::capture
{
    /** @brief Per-frame callback invoked at each EndScene on the main thread with the engine device. */
    using FrameFn = void (*)(IDirect3DDevice9* device);

    /**
     * @brief Wraps the real factory so CreateDevice and CreateDeviceEx are intercepted.
     * @param real  real IDirect3D9 factory.
     * @return Forwarding wrapper to hand back to the engine, or null if real is null.
     */
    IDirect3D9*   Wrap(IDirect3D9* real);

    /**
     * @brief Wraps the real Ex factory so CreateDevice and CreateDeviceEx are intercepted.
     * @param real  real IDirect3D9Ex factory.
     * @return Forwarding wrapper to hand back to the engine, or null if real is null.
     */
    IDirect3D9Ex* WrapEx(IDirect3D9Ex* real);

    /**
     * @brief Registers the per-frame callback; call before the engine creates its device.
     * @param fn  callback invoked at each EndScene.
     */
    void OnFrame(FrameFn fn);

    /**
     * @brief Returns the captured engine device.
     * @return The device, or null until the engine has called CreateDevice.
     */
    IDirect3DDevice9* Device();
}
