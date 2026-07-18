// M2 render event context: per-instance model draw/update/skin/bone/ribbon hooks exposed to wxl.on.
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

/// One event context declaring the M2 (model) render-path events into the shared bridge. Raw engine
/// pointers (model / render context / ribbon emitter) are passed through as LIGHTUSERDATA for a signed
/// extension to ffi.cast against the M0 cdefs — never dereferenced here, so no SEH guard is needed.
/// These fire on the hot per-instance render path; the bus Any()-gates each emit so they cost nothing
/// unless an extension has subscribed. A handler that mutates engine state (e.g. bone matrices) does so
/// by writing through the passed pointer via ffi. Events exposed here:
///   "m2_skin_finalize" -> OnM2SkinFinalize    fn(model)                      a model's skin profile is being finalized (lightuserdata).
///   "m2_update"        -> OnM2PerFrameUpdate   fn(renderCtx)                  per-instance scene-graph update tick (lightuserdata).
///   "bone_palette"     -> OnBuildBonePalette   fn(renderCtx)                  the per-instance bone palette was just filled; a handler
///                          may overwrite bone matrices through renderCtx (lightuserdata) to override the pose. No bool out-param.
///   "m2_batch_draw"    -> OnM2BatchDraw        fn(device, model, primType, baseVertex, minIndex, numVerts, startIndex, primCount)
///                          device and model are lightuserdata; the six draw parameters are ints. Re-issue the draw here.
///   "m2_setup_alpha"   -> OnM2SetupBatchAlpha  fn(model, blendMode)           an M2 batch's alpha/material was set up.
///                          model is lightuserdata (may be a NULL lightuserdata -> skip); blendMode is int (1 = alpha key).
///   "ribbon_draw"      -> OnRibbonDraw         fn(emitter, layerCount) -> boolean  a ribbon emitter is about to draw.
///                          emitter is lightuserdata; layerCount is int. RETURN true to request the single-pass
///                          multi-texture combine (sets *useMultiTexture) for a ribbon of >= 3 layers.
namespace wxl::lua::events::m2
{
    /// OnM2SkinFinalize: push the model pointer as lightuserdata (M2SkinFinalizeArgs{model}).
    inline int PushSkinFinalize(lua_State* L, const void* args)
    {
        lua_pushlightuserdata(L, static_cast<const wxl::events::M2SkinFinalizeArgs*>(args)->model);
        return 1;
    }

    /// OnM2PerFrameUpdate: push the render context as lightuserdata (M2PerFrameUpdateArgs{renderCtx}).
    inline int PushPerFrameUpdate(lua_State* L, const void* args)
    {
        lua_pushlightuserdata(L, static_cast<const wxl::events::M2PerFrameUpdateArgs*>(args)->renderCtx);
        return 1;
    }

    /// OnBuildBonePalette: push the render context as lightuserdata (BuildBonePaletteArgs{renderCtx}).
    /// Write-back (overriding bone matrices) is done by the handler mutating through this pointer.
    inline int PushBonePalette(lua_State* L, const void* args)
    {
        lua_pushlightuserdata(L, static_cast<const wxl::events::BuildBonePaletteArgs*>(args)->renderCtx);
        return 1;
    }

    /// OnM2BatchDraw: push device + model (lightuserdata) then the six draw parameters as ints.
    inline int PushBatchDraw(lua_State* L, const void* args)
    {
        const auto* a = static_cast<const wxl::events::M2BatchDrawArgs*>(args);
        lua_pushlightuserdata(L, a->device);
        lua_pushlightuserdata(L, a->model);
        lua_pushinteger(L, static_cast<lua_Integer>(a->primType));
        lua_pushinteger(L, static_cast<lua_Integer>(a->baseVertex));
        lua_pushinteger(L, static_cast<lua_Integer>(a->minIndex));
        lua_pushinteger(L, static_cast<lua_Integer>(a->numVerts));
        lua_pushinteger(L, static_cast<lua_Integer>(a->startIndex));
        lua_pushinteger(L, static_cast<lua_Integer>(a->primCount));
        return 8;
    }

    /// OnM2SetupBatchAlpha: push the model pointer (lightuserdata, may be NULL) then the blend mode int.
    inline int PushSetupAlpha(lua_State* L, const void* args)
    {
        const auto* a = static_cast<const wxl::events::M2SetupBatchAlphaArgs*>(args);
        lua_pushlightuserdata(L, a->model);
        lua_pushinteger(L, static_cast<lua_Integer>(a->blendMode));
        return 2;
    }

    /// OnRibbonDraw: push the emitter (lightuserdata) then the layer count int. The return value is
    /// consumed by ConsumeRibbon.
    inline int PushRibbonDraw(lua_State* L, const void* args)
    {
        const auto* a = static_cast<const wxl::events::RibbonDrawArgs*>(args);
        lua_pushlightuserdata(L, a->emitter);
        lua_pushinteger(L, static_cast<lua_Integer>(a->layerCount));
        return 2;
    }

    /// A truthy return from a ribbon_draw handler requests the single-pass multi-texture combine
    /// (sets *useMultiTexture).
    inline void ConsumeRibbon(const void* args, bool ret)
    {
        if (!ret)
            return;
        const auto* a = static_cast<const wxl::events::RibbonDrawArgs*>(args);
        if (a->useMultiTexture)
            *a->useMultiTexture = true;
    }

    /// Registers this context's events with the bridge. Called once at install, before SubscribeBus.
    inline void Declare()
    {
        using wxl::events::Event;
        events::Declare("m2_skin_finalize", Event::OnM2SkinFinalize,    &PushSkinFinalize);
        events::Declare("m2_update",        Event::OnM2PerFrameUpdate,  &PushPerFrameUpdate);
        events::Declare("bone_palette",     Event::OnBuildBonePalette,  &PushBonePalette);
        events::Declare("m2_batch_draw",    Event::OnM2BatchDraw,       &PushBatchDraw);
        events::Declare("m2_setup_alpha",   Event::OnM2SetupBatchAlpha, &PushSetupAlpha);
        events::Declare("ribbon_draw",      Event::OnRibbonDraw,        &PushRibbonDraw, &ConsumeRibbon);
    }
}
