// Asset event context: model/texture/geometry load, character item slots, host launch for wxl.on.
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

/// One event context declaring the asset/geometry-load, character-model item, and asset-host-launch
/// events into the shared bridge. Raw engine pointers are passed through as LIGHTUSERDATA for a signed
/// extension to ffi.cast against the M0 cdefs — they are never dereferenced here, so no marshaller
/// reads game memory and none needs an SEH guard. A NULL pointer arrives as a NULL lightuserdata
/// (ffi.cast yields a NULL cdata), not nil. Events exposed here:
///   "blp_load"           -> OnBlpLoad         fn(name, handle)                  a BLP texture resolved by name.
///                            name is a string (nil if unavailable); handle is lightuserdata (the texture handle).
///   "texture_upload"     -> OnTextureUpload   fn(width, height, texture)        a texture is uploading to the device.
///                            width/height are ints; texture is lightuserdata (the device texture).
///   "model_load_pre"     -> OnModelLoadPre    fn(model)                         a model's raw bytes are read, pre-parse.
///   "model_load"         -> OnModelLoad       fn(model)                         a model finished loading and parsed.
///                            model is lightuserdata.
///   "wmo_root_load"      -> OnWmoRootLoad     fn(root)                          a WMO root buffer was read, pre-walk (lightuserdata).
///   "wmo_group_load"     -> OnWmoGroupLoad    fn(group)                         a WMO group buffer was read, pre-walk (lightuserdata).
///   "adt_chunk"          -> OnAdtChunkBuild   fn(layerCount, chunk)             an ADT map chunk is being built.
///                            layerCount is int; chunk is lightuserdata.
///   "adt_split_tile_load"-> OnAdtSplitTileLoad fn(tileX, tileY, rootSize, texSize, objSize, chunks)
///                            a split (Cata+) ADT tile finished its root/_tex0/_obj0 load; all ints.
///   "doodad_spawn"       -> OnDoodadSpawn     fn(doodad)                        a placed map doodad was built (lightuserdata).
///   "item_slot_change"   -> OnItemSlotChange  fn(modelSlot, charModel, itemData) a char-model slot received an item.
///                            modelSlot is int; charModel and itemData are lightuserdata.
///   "item_slot_clear"    -> OnItemSlotClear   fn(equipSlotWow, charModel)       a char-model equipment slot was cleared.
///                            equipSlotWow is int (EQUIPMENT_SLOT_*, 0-18); charModel is lightuserdata.
///   "before_host_launch" -> OnBeforeHostLaunch fn(exePath) -> boolean           the DLL is about to launch the asset host.
///                            exePath is a string; RETURN true to cancel the auto-launch (sets *cancel).
namespace wxl::lua::events::asset
{
    /// OnBlpLoad: push the requested virtual path (string, nil if null) then the resolved texture
    /// handle as lightuserdata (may be a NULL lightuserdata on a failed resolve).
    inline int PushBlpLoad(lua_State* L, const void* args)
    {
        const auto* a = static_cast<const wxl::events::BlpLoadArgs*>(args);
        if (a->name)
            lua_pushstring(L, a->name);
        else
            lua_pushnil(L);
        lua_pushlightuserdata(L, a->handle);
        return 2;
    }

    /// OnTextureUpload: push the pixel dimensions (ints) then the device texture as lightuserdata.
    inline int PushTextureUpload(lua_State* L, const void* args)
    {
        const auto* a = static_cast<const wxl::events::TextureUploadArgs*>(args);
        lua_pushinteger(L, static_cast<lua_Integer>(a->width));
        lua_pushinteger(L, static_cast<lua_Integer>(a->height));
        lua_pushlightuserdata(L, a->texture);
        return 3;
    }

    /// OnModelLoadPre / OnModelLoad: push the model pointer as lightuserdata (shared ModelLoadArgs).
    inline int PushModelLoad(lua_State* L, const void* args)
    {
        lua_pushlightuserdata(L, static_cast<const wxl::events::ModelLoadArgs*>(args)->model);
        return 1;
    }

    /// OnWmoRootLoad: push the root buffer pointer as lightuserdata.
    inline int PushWmoRoot(lua_State* L, const void* args)
    {
        lua_pushlightuserdata(L, static_cast<const wxl::events::WmoRootLoadArgs*>(args)->root);
        return 1;
    }

    /// OnWmoGroupLoad: push the group buffer pointer as lightuserdata.
    inline int PushWmoGroup(lua_State* L, const void* args)
    {
        lua_pushlightuserdata(L, static_cast<const wxl::events::WmoGroupLoadArgs*>(args)->group);
        return 1;
    }

    /// OnAdtChunkBuild: push the texture-layer count (int) then the chunk pointer as lightuserdata.
    inline int PushAdtChunk(lua_State* L, const void* args)
    {
        const auto* a = static_cast<const wxl::events::AdtChunkArgs*>(args);
        lua_pushinteger(L, static_cast<lua_Integer>(a->layerCount));
        lua_pushlightuserdata(L, a->chunk);
        return 2;
    }

    /// OnAdtSplitTileLoad: push the tile indices, the three resident buffer sizes and the indexed
    /// chunk count as plain integers (no pointers cross into Lua here).
    inline int PushAdtSplitTileLoad(lua_State* L, const void* args)
    {
        const auto* a = static_cast<const wxl::events::AdtSplitTileLoadArgs*>(args);
        lua_pushinteger(L, a->tileFirst);
        lua_pushinteger(L, a->tileSecond);
        lua_pushinteger(L, static_cast<lua_Integer>(a->rootSize));
        lua_pushinteger(L, static_cast<lua_Integer>(a->texSize));
        lua_pushinteger(L, static_cast<lua_Integer>(a->objSize));
        lua_pushinteger(L, static_cast<lua_Integer>(a->chunkCount));
        return 6;
    }

    /// OnDoodadSpawn: push the doodad pointer as lightuserdata.
    inline int PushDoodadSpawn(lua_State* L, const void* args)
    {
        lua_pushlightuserdata(L, static_cast<const wxl::events::DoodadSpawnArgs*>(args)->doodad);
        return 1;
    }

    /// OnItemSlotChange: push the internal model slot index (int) then the char-model and item-data
    /// pointers as lightuserdata.
    inline int PushItemSlotChange(lua_State* L, const void* args)
    {
        const auto* a = static_cast<const wxl::events::ItemSlotChangeArgs*>(args);
        lua_pushinteger(L, static_cast<lua_Integer>(a->modelSlot));
        lua_pushlightuserdata(L, a->charModelObj);
        lua_pushlightuserdata(L, a->itemDataPtr);
        return 3;
    }

    /// OnItemSlotClear: push the WoW equipment slot index (int) then the char-model as lightuserdata.
    inline int PushItemSlotClear(lua_State* L, const void* args)
    {
        const auto* a = static_cast<const wxl::events::ItemSlotClearArgs*>(args);
        lua_pushinteger(L, static_cast<lua_Integer>(a->equipSlotWow));
        lua_pushlightuserdata(L, a->charModelObj);
        return 2;
    }

    /// OnBeforeHostLaunch: push the exe path (string). The return value is consumed by ConsumeHostLaunch.
    inline int PushHostLaunch(lua_State* L, const void* args)
    {
        const char* p = static_cast<const wxl::events::HostLaunchArgs*>(args)->exePath;
        if (p)
            lua_pushstring(L, p);
        else
            lua_pushnil(L);
        return 1;
    }

    /// A truthy return from a before_host_launch handler cancels the auto-launch (sets *cancel).
    inline void ConsumeHostLaunch(const void* args, bool ret)
    {
        if (!ret)
            return;
        const auto* a = static_cast<const wxl::events::HostLaunchArgs*>(args);
        if (a->cancel)
            *a->cancel = true;
    }

    /// Registers this context's events with the bridge. Called once at install, before SubscribeBus.
    inline void Declare()
    {
        using wxl::events::Event;
        events::Declare("blp_load",           Event::OnBlpLoad,          &PushBlpLoad);
        events::Declare("texture_upload",     Event::OnTextureUpload,    &PushTextureUpload);
        events::Declare("model_load_pre",     Event::OnModelLoadPre,     &PushModelLoad);
        events::Declare("model_load",         Event::OnModelLoad,        &PushModelLoad);
        events::Declare("wmo_root_load",      Event::OnWmoRootLoad,      &PushWmoRoot);
        events::Declare("wmo_group_load",     Event::OnWmoGroupLoad,     &PushWmoGroup);
        events::Declare("adt_chunk",          Event::OnAdtChunkBuild,    &PushAdtChunk);
        events::Declare("adt_split_tile_load", Event::OnAdtSplitTileLoad, &PushAdtSplitTileLoad);
        events::Declare("doodad_spawn",       Event::OnDoodadSpawn,      &PushDoodadSpawn);
        events::Declare("item_slot_change",   Event::OnItemSlotChange,   &PushItemSlotChange);
        events::Declare("item_slot_clear",    Event::OnItemSlotClear,    &PushItemSlotClear);
        events::Declare("before_host_launch", Event::OnBeforeHostLaunch, &PushHostLaunch, &ConsumeHostLaunch);
    }
}
