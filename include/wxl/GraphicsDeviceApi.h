// Graphics device provider API: lets an extension supply the engine's CGxDevice backend.
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

#ifndef WXL_GRAPHICS_DEVICE_API_H
#define WXL_GRAPHICS_DEVICE_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WXL_GRAPHICS_DEVICE_API_NAME "wxl.graphics.device"
#define WXL_GRAPHICS_DEVICE_API_VERSION 2

/**
 * @brief Factory called during the engine's GxDevCreate, in place of the stock CGxDevice::New*.
 *
 * It constructs the device and nothing more: the Blizzard base (InitBaseDevice) and the backend's own
 * vtable over it. The core then does what GxDevCreate does with a stock device -- publishes it as the
 * engine device, then calls its DeviceCreate (vtable slot 10) with wndProc and context. When that
 * fails the core deletes it through slot 8 (flags 1) and lets the engine build its stock backend.
 *
 * @param api      the API the client asked for (0 OpenGL, 1 D3D9, 2 D3D9Ex).
 * @param wndProc  the window procedure, passed on to DeviceCreate.
 * @param context  the CGxFormat, passed on to DeviceCreate.
 * @return the backend's CGxDevice, or NULL to let the engine build the stock one.
 */
typedef void* (__cdecl* WXL_DeviceCreateFn)(int api, void* wndProc, void* context);

typedef struct WXL_GraphicsDeviceApi
{
    uint32_t structSize;
    uint32_t apiVersion;

    /// Registers the device factory. One at a time: a second registration replaces the first.
    int (__cdecl* RegisterFactory)(WXL_DeviceCreateFn factory, const char* name);

    /// Unregisters @p factory when it is the registered one, so the engine goes back to its own.
    void (__cdecl* UnregisterFactory)(WXL_DeviceCreateFn factory);

    /// Runs the Blizzard CGxDevice constructor on @p dev, then its default pools and stream buffers.
    void (__cdecl* InitBaseDevice)(void* dev);

    /// The engine's base CGxDevice vtable, which a backend copies and overrides slot by slot.
    const void* const* (__cdecl* GetBaseVTable)(void);
} WXL_GraphicsDeviceApi;

#ifdef __cplusplus
}
#endif

#endif // WXL_GRAPHICS_DEVICE_API_H
