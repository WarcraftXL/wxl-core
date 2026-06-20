// The d3d9.dll proxy exports that hand the shared D3D12 device + queue to WarcraftXL.dll.
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

// Private exports of the d3d9.dll proxy that hand the shared D3D12 device and queue (the ones On12 runs
// on) to WarcraftXL.dll, so a D3D12 backend renders on the same queue as the engine.
extern "C"
{
    /** @brief Returns the shared D3D12 device backing On12. @return The shared device. */
    __declspec(dllimport) ID3D12Device*       WxlD3D12Device();

    /** @brief Returns the shared render queue backing On12. @return The shared command queue. */
    __declspec(dllimport) ID3D12CommandQueue* WxlD3D12Queue();

    /** @brief Flushes the D3D12 debug-layer validation messages to the proxy log. */
    __declspec(dllimport) void                WxlD3D12DrainDebug();
}
