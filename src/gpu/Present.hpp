// Owns the present: shows the engine's backbuffer through our own DXGI flip swapchain on the window.
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

#include <windows.h>
#include <d3d9.h>

/**
 * @brief Owns the present so the engine's frame reaches the window in every mode.
 *
 * The d3d9on12 mapping layer's own windowed (DWM-composited) present does not show the backbuffer, so the
 * proxy presents the frame itself: it copies the engine's D3D9 backbuffer (unwrapped to D3D12 through On12)
 * into its own DXGI flip-model swapchain on the engine's window and presents that swapchain, bypassing the
 * On12 present entirely. This path works both windowed and fullscreen.
 */
namespace wxl::gpu::present
{
    /**
     * @brief Drains proxy presentation work before D3D9 Reset.
     * @return true when it is safe to enter the native reset; false when the GPU did not drain in time.
     */
    bool PrepareForReset();

    /**
     * @brief Presents the engine's current backbuffer through the proxy's own swapchain.
     *
     * Lazily creates (and resizes) the swapchain on the device's window. On success the caller must not
     * call the engine's native present, since this replaces it.
     * @param device  the live engine D3D9 device whose backbuffer is shown.
     * @param window  the engine's render window (the swapchain target).
     * @return true when the frame was presented through our swapchain; false to fall back to the native present.
     */
    bool Present(IDirect3DDevice9* device, HWND window);

    /**
     * @brief True when a real native Reset must not be skipped.
     *
     * Set when the window was iconified or a present submit failed since the last real Reset: an
     * engine Reset with UNCHANGED params right after either event is On12's recovery path, not a
     * parasite — skipping it wedges the stack (observed DXGI_ERROR_DEVICE_HUNG, unrecoverable).
     * TestCooperativeLevel cannot stand in for this signal: On12 reports D3D_OK throughout.
     * @return true when the next Reset must run natively.
     */
    bool ResetRequired();

    /** @brief Records that a real native Reset ran; clears the ResetRequired latch. */
    void MarkRealReset();
}
