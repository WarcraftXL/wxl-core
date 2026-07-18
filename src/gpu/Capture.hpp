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
#include <d3d12.h>

/**
 * @brief Captures the engine's D3D9 device by intercepting IDirect3D9::CreateDevice on the main thread.
 */
namespace wxl::gpu::capture
{
    /** @brief Per-frame callback invoked at each EndScene on the main thread with the engine device. */
    using FrameFn = void (*)(IDirect3DDevice9* device);

    /**
     * @brief Wraps the real factory so CreateDevice and CreateDeviceEx are intercepted.
     *
     * The engine creates several On12 factories (a probe first, the real one later), each on its own On12
     * queue. queue is the On12 queue this factory runs on, recorded when this factory creates the captured
     * device so the unwrap/submit path uses the presenting device's queue.
     * @param real   real IDirect3D9 factory.
     * @param queue  the On12 queue this factory was created on.
     * @return Forwarding wrapper to hand back to the engine, or null if real is null.
     */
    IDirect3D9*   Wrap(IDirect3D9* real, ID3D12CommandQueue* queue);

    /**
     * @brief Wraps the real Ex factory so CreateDevice and CreateDeviceEx are intercepted.
     * @param real   real IDirect3D9Ex factory.
     * @param queue  the On12 queue this factory was created on.
     * @return Forwarding wrapper to hand back to the engine, or null if real is null.
     */
    IDirect3D9Ex* WrapEx(IDirect3D9Ex* real, ID3D12CommandQueue* queue);

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

    /**
     * @brief Returns the On12 queue of the captured (presenting) engine device.
     *
     * This is the queue the presenting device's On12 runs on, which is the queue a post-process pass must
     * unwrap, submit, and signal on so its work is synchronized with the present.
     * @return The presenting device's queue, or null until the engine has called CreateDevice.
     */
    ID3D12CommandQueue* PresentQueue();

    /**
     * @brief Sets the supersampling factor applied to the windowed backbuffer (1.0 = off).
     *
     * The factor scales the backbuffer the engine renders into; the proxy's present downsamples it to the
     * window. It takes effect on the next device create/reset (e.g. a resolution change), not immediately.
     * @param factor  1.0 (off), 1.5, 2.0, ...
     */
    void SetSsaaFactor(float factor);

    /** @brief Returns the current supersampling factor. @return The factor (1.0 = off). */
    float SsaaFactor();
}
