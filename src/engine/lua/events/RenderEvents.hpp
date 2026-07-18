// Render event context: frame/device lifecycle + world-render boundary + liquid pass for wxl.on.
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

#include "engine/lua/LuaJit.hpp"
#include "engine/lua/events/EventBridge.hpp"
#include "engine/events/Event.hpp"

/// One event context declaring the frame/device lifecycle, world-render boundary, and liquid-pass
/// events into the shared bridge. Raw D3D pointers are passed through as LIGHTUSERDATA for a signed
/// extension to ffi.cast against the M0 cdefs — never dereferenced here, so no SEH guard is needed.
/// The per-frame events (end_scene/world_render/world_render_end) fire every frame, but the bus
/// Any()-gates the emit so they cost nothing unless an extension has subscribed. ("frame" -> OnFrame
/// lives in FrameEvents alongside update/world_enter/world_leave, and pushes the device there.)
/// Events exposed here:
///   "end_scene"        -> OnEndScene         fn(device)                    end of the frame, before present (lightuserdata).
///   "device_lost"      -> OnDeviceLost       fn(device, params)            before the D3D9 device Reset frees DEFAULT resources.
///   "device_reset"     -> OnDeviceReset      fn(device, params)            after a successful D3D9 device Reset.
///                          device and params (D3DPRESENT_PARAMETERS*) are lightuserdata.
///   "world_render"     -> OnWorldRender      fn(device)                    per-frame world draw pass began (lightuserdata).
///   "world_render_end" -> OnWorldRenderEnd   fn(device)                    world -> UI boundary, the post-fx slot (lightuserdata).
///   "liquid_render"    -> OnLiquidRender      fn(passType, instanceCount, bank, transform) a liquid render pass is about to draw.
///                          passType (0 main, 1 secondary) and instanceCount are ints; bank and transform are lightuserdata.
namespace wxl::lua::events::render
{
    /// OnEndScene: push the device pointer as lightuserdata (EndSceneArgs{device}).
    inline int PushEndScene(lua_State* L, const void* args)
    {
        lua_pushlightuserdata(L, static_cast<const wxl::events::EndSceneArgs*>(args)->device);
        return 1;
    }

    /// OnDeviceLost / OnDeviceReset: push the device then the present-params as lightuserdata (shared
    /// DeviceResetArgs).
    inline int PushDeviceReset(lua_State* L, const void* args)
    {
        const auto* a = static_cast<const wxl::events::DeviceResetArgs*>(args);
        lua_pushlightuserdata(L, a->device);
        lua_pushlightuserdata(L, a->params);
        return 2;
    }

    /// OnWorldRender: push the device pointer as lightuserdata (WorldRenderArgs{device}).
    inline int PushWorldRender(lua_State* L, const void* args)
    {
        lua_pushlightuserdata(L, static_cast<const wxl::events::WorldRenderArgs*>(args)->device);
        return 1;
    }

    /// OnWorldRenderEnd: push the device pointer as lightuserdata (WorldRenderEndArgs{device}).
    inline int PushWorldRenderEnd(lua_State* L, const void* args)
    {
        lua_pushlightuserdata(L, static_cast<const wxl::events::WorldRenderEndArgs*>(args)->device);
        return 1;
    }

    /// OnLiquidRender: push pass type + instance count (ints), then the bank and transform pointers as
    /// lightuserdata.
    inline int PushLiquidRender(lua_State* L, const void* args)
    {
        const auto* a = static_cast<const wxl::events::LiquidRenderArgs*>(args);
        lua_pushinteger(L, static_cast<lua_Integer>(a->passType));
        lua_pushinteger(L, static_cast<lua_Integer>(a->instanceCount));
        lua_pushlightuserdata(L, a->bank);
        lua_pushlightuserdata(L, a->transform);
        return 4;
    }

    /// Registers this context's events with the bridge. Called once at install, before SubscribeBus.
    inline void Declare()
    {
        using wxl::events::Event;
        events::Declare("end_scene",        Event::OnEndScene,       &PushEndScene);
        events::Declare("device_lost",      Event::OnDeviceLost,     &PushDeviceReset);
        events::Declare("device_reset",     Event::OnDeviceReset,    &PushDeviceReset);
        events::Declare("world_render",     Event::OnWorldRender,    &PushWorldRender);
        events::Declare("world_render_end", Event::OnWorldRenderEnd, &PushWorldRenderEnd);
        events::Declare("liquid_render",    Event::OnLiquidRender,   &PushLiquidRender);
    }
}
