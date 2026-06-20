// D3D12 plumbing for the d3d9 proxy: one shared device + render queue backing the On12 stack.
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

#include <d3d12.h>
#include <d3d9on12.h>

/**
 * @brief D3D12 plumbing for the proxy: one shared D3D12 device and render queue back the whole stack.
 *
 * The engine creates several D3D9 devices; the first (active, presented) one runs On12 on the shared
 * queue, keeping the unwrap and return path single-queue. Later devices get a throwaway separate queue.
 */
namespace wxl::gpu
{
    /**
     * @brief Returns the shared D3D12 device, created on first use.
     * @return The shared device.
     */
    ID3D12Device*       Device();

    /**
     * @brief Returns the shared render queue, which is the first device's On12 queue.
     * @return The shared command queue.
     */
    ID3D12CommandQueue* Queue();

    /**
     * @brief Builds On12 args; the first call uses the shared device and queue, later calls use the shared
     *        device with a fresh queue.
     * @return Populated D3D9ON12_ARGS.
     */
    D3D9ON12_ARGS MakeOn12Args();

    /** @brief Drains the D3D12 debug layer's stored validation messages to the log, a no-op if absent. */
    void DrainDebug();

    /**
     * @brief Writes a formatted line to the shared diagnostic log for the d3d9 proxy.
     * @param fmt  printf-style format string followed by its arguments.
     */
    void Log(const char* fmt, ...);
}
