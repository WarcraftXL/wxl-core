// The device-provider seam: an extension supplies the engine's CGxDevice instead of the stock backend.
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

// Every renderer the client has runs behind one abstraction: a CGxDevice, an object of 84 virtual
// slots that the engine never looks past. Nothing in game code calls a graphics API directly, so a
// different renderer is a different object in that one slot -- not a change anywhere else.
//
// The engine builds it in GxDevCreate, which picks a stock backend from the requested API. Detouring
// that is what lets an extension answer with its own device. The core then repeats, verbatim, what
// GxDevCreate does with a stock one: publish the device where the engine reads it, then call its
// DeviceCreate. The order matters -- the create reaches shader and texture code that goes looking for
// the device through the published pointer, so publishing after the create is too late.
//
// The engine's own constructor still has to run on the object: it builds the matrix stacks, the
// render-state arrays, the TSLists and the default pools that the base class and the engine both
// read, and a backend that skipped it would be handing the engine half an object. A backend calls
// InitBaseDevice for that and keeps its own vtable over the result; this file does it as a safety net
// for one that forgot, since the symptom otherwise is a crash far from the cause.

#include "common/Log.hpp"
#include "engine/hook/Registry.hpp"
#include "runtime/Extensions.hpp"
#include "runtime/HookPoints.hpp"
#include "wxl/GraphicsDeviceApi.h"
#include "wxl/game/Binding.hpp"
#include "wxl/offsets/engine/Gx.hpp"
#include "wxl/offsets/engine/GxDevice.hpp"

#include <cstdint>
#include <cstring>

namespace
{
    namespace gxoff  = wxl::offsets::engine::gx;
    namespace devoff = wxl::offsets::engine::gxdevice;

    devoff::DevCreateFn g_origDevCreate = nullptr;

    WXL_DeviceCreateFn g_factory = nullptr;
    char               g_backendName[64] = "unknown";

    int __cdecl ApiRegisterFactory(WXL_DeviceCreateFn factory, const char* name)
    {
        if (!factory) return 0;
        g_factory = factory;
        std::strncpy(g_backendName, name && name[0] ? name : "unnamed", sizeof g_backendName - 1);
        g_backendName[sizeof g_backendName - 1] = '\0';
        WLOG_INFO("gx-device: device factory registered by %s", g_backendName);
        return 1;
    }

    void __cdecl ApiUnregisterFactory(WXL_DeviceCreateFn factory)
    {
        if (g_factory != factory) return;
        WLOG_INFO("gx-device: device factory of %s withdrawn", g_backendName);
        g_factory = nullptr;
        std::strcpy(g_backendName, "unknown");
    }

    void __cdecl ApiInitBaseDevice(void* dev)
    {
        if (!dev) return;

        wxl::game::Native<devoff::DeviceCtorFn>(devoff::kDeviceCtor)(dev, nullptr);
        // The two the base constructor does not do, and every stock backend's constructor does: the
        // default stream pools and the CGxBufs on them. The shader system releases through those bufs
        // at shutdown, so a device without them crashes on exit rather than at create.
        wxl::game::Native<devoff::DeviceCreatePoolsFn>(devoff::kDeviceCreatePools)(dev, nullptr);
        wxl::game::Native<devoff::DeviceCreatePoolsFn>(devoff::kDeviceCreateStreamBufs)(dev, nullptr);

        // A non-D3D9 backend leaves no D3D device behind: the field is read by engine code that tests
        // it before using it, so it has to be null rather than whatever the allocation held.
        wxl::game::At<void*>(dev, gxoff::kD3DDeviceField) = nullptr;
    }

    const void* const* __cdecl ApiGetBaseVTable(void)
    {
        return reinterpret_cast<const void* const*>(devoff::kBaseVTable);
    }

    const WXL_GraphicsDeviceApi g_api = {
        sizeof(WXL_GraphicsDeviceApi),
        WXL_GRAPHICS_DEVICE_API_VERSION,
        &ApiRegisterFactory,
        &ApiUnregisterFactory,
        &ApiInitBaseDevice,
        &ApiGetBaseVTable,
    };

    /// True once the engine's constructor has run on @p dev: it links the pool list's terminator to
    /// itself. The list's first dword is its link offset, which the constructor writes as 0, so it
    /// says nothing; the terminator's prev and next links are never null on a constructed device.
    bool BaseRan(void* dev)
    {
        return wxl::game::At<uintptr_t>(dev, devoff::kPoolListPrev) != 0
            && wxl::game::At<uintptr_t>(dev, devoff::kPoolListNext) != 0;
    }

    void* __cdecl hkDevCreate(int api, void* wndProc, void* context)
    {
        // The client builds its device before it reaches the engine-init seam the extensions load
        // from, so they are loaded here instead: a backend can only answer once its WXL_Load has
        // registered the factory. Without one, everything below is the engine's own path.
        wxl::runtime::extensions::EnsureLoaded();

        if (!g_factory) return g_origDevCreate(api, wndProc, context);

        WLOG_INFO("gx-device: GxDevCreate(api %d) offered to %s", api, g_backendName);
        void* dev = g_factory(api, wndProc, context);
        if (!dev)
        {
            WLOG_WARN("gx-device: %s declined; the engine builds its stock backend", g_backendName);
            return g_origDevCreate(api, wndProc, context);
        }

        // The backend was meant to do this itself. Running the constructor now would also overwrite
        // the vtable it installed, so put that back.
        if (!BaseRan(dev))
        {
            WLOG_WARN("gx-device: %s returned a device the base constructor had not run on", g_backendName);
            void* vtable = *static_cast<void**>(dev);
            ApiInitBaseDevice(dev);
            *static_cast<void**>(dev) = vtable;
        }
        else
        {
            WLOG_INFO("gx-device: %s's device carries the base constructor (pool list linked), "
                      "not run again", g_backendName);
        }

        // GxDevCreate's own sequence, in its own order.
        auto** published = reinterpret_cast<void**>(gxoff::kGxDevicePtr);
        *published = dev;

        using DeviceCreateFn = int(__fastcall*)(void* device, void* edx, void* wndProc, void* format);
        using ScalarDeleteFn = void*(__fastcall*)(void* device, void* edx, uint32_t flags);
        auto** vtbl = *reinterpret_cast<void***>(dev);

        if (reinterpret_cast<DeviceCreateFn>(vtbl[devoff::kSlotDeviceCreate])(dev, nullptr, wndProc, context))
        {
            WLOG_INFO("gx-device: %s is the engine device, at %p", g_backendName, dev);
            return dev;
        }

        WLOG_WARN("gx-device: %s failed to create; the engine builds its stock backend", g_backendName);
        reinterpret_cast<ScalarDeleteFn>(vtbl[devoff::kSlotScalarDelete])(dev, nullptr, 1);
        *published = nullptr;
        return g_origDevCreate(api, wndProc, context);
    }

    bool InstallDeviceCreate()
    {
        if (!wxl::runtime::hookpoints::Attach("Gx.DevCreate", &hkDevCreate, &g_origDevCreate))
        {
            WLOG_ERROR("gx-device: Gx.DevCreate did not attach; no extension can supply a device");
            return false;
        }

        // Published in the Boot phase, before any extension is loaded: the table takes a service with
        // nobody waiting on it, and an extension's WXL_Load is what later asks for it.
        wxl::runtime::extensions::PublishInterface(WXL_GRAPHICS_DEVICE_API_NAME, WXL_GRAPHICS_DEVICE_API_VERSION,
                                                   const_cast<WXL_GraphicsDeviceApi*>(&g_api));
        return true;
    }
}

WXL_REGISTER_FEATURE_PHASED("gx-device-create", true, InstallDeviceCreate, ::wxl::hook::Phase::Boot)
