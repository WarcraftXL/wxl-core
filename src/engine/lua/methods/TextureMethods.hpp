// Texture method context: the wxl.texture(path) loader and its handle userdata (width/height/size/id).
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
#include "engine/lua/ui/Texture.hpp"

#include "imgui.h" // ImTextureID / ImU64

/// The wxl.texture context, in the CoreMethods/CameraMethods mould: Register() adds the callable
/// `wxl.texture` to the `wxl` table on the stack, and RegisterTextureMeta() creates the userdata metatable
/// once (like ObjectProxy's RegisterMetatables). `wxl.texture(path)` loads or returns a cached BLP as a
/// full-userdata handle carrying the "wxl.texture" metatable; the handle's __index exposes width/height/
/// size/id. The handle is a thin reference into the loader's cache -- the cache owns the D3D texture, so
/// there is deliberately NO __gc (releasing on collection would break the shared cache entry). CheckTextureId
/// resolves either a handle or a raw number to an ImTextureID and is shared by wxl.ui.image / add_image.
namespace wxl::lua::methods::texture
{
    /// Metatable name backing a texture handle; also the luaL_checkudata type tag.
    inline constexpr const char* kTextureMeta = "wxl.texture";

    /// Pushes a full-userdata handle wrapping a cache-owned Texture* (never null here).
    inline void PushTexture(lua_State* L, wxl::lua::ui::Texture* t)
    {
        auto** ud = static_cast<wxl::lua::ui::Texture**>(lua_newuserdata(L, sizeof(wxl::lua::ui::Texture*)));
        *ud = t;
        luaL_getmetatable(L, kTextureMeta);
        lua_setmetatable(L, -2);
    }

    /// Returns the userdata pointer if the value at idx is a texture handle, else nullptr. Hand-rolled
    /// because LuaJIT (5.1) has no luaL_testudata.
    inline void* TestTexture(lua_State* L, int idx)
    {
        void* p = lua_touserdata(L, idx);
        if (!p || !lua_getmetatable(L, idx))
            return nullptr;
        luaL_getmetatable(L, kTextureMeta);
        const bool same = lua_rawequal(L, -1, -2) != 0;
        lua_pop(L, 2);
        return same ? p : nullptr;
    }

    /// Retrieves the cache-owned Texture* from a handle at idx, raising a typed error otherwise.
    inline wxl::lua::ui::Texture* CheckTexture(lua_State* L, int idx)
    {
        void* ud = luaL_checkudata(L, idx, kTextureMeta);
        return *static_cast<wxl::lua::ui::Texture**>(ud);
    }

    /// Resolves an image source argument to an ImTextureID: a wxl.texture handle (preferred) or a raw
    /// numeric id. Raises a Lua error for anything else. Shared by wxl.ui.image and drawlist:add_image.
    inline ImTextureID CheckTextureId(lua_State* L, int idx)
    {
        if (void* ud = TestTexture(L, idx))
            return wxl::lua::ui::TexId(*static_cast<wxl::lua::ui::Texture**>(ud));
        if (lua_isnumber(L, idx))
            return static_cast<ImTextureID>(static_cast<ImU64>(lua_tonumber(L, idx)));
        luaL_error(L, "expected a wxl.texture handle or a numeric texture id at argument %d", idx);
        return static_cast<ImTextureID>(0); // unreachable
    }

    /// wxl.texture(path) -> handle | nil: loads or returns the cached BLP at path (game-I/O resolved,
    /// self-decoded, uploaded to a managed IDirect3DTexture9). nil on any failure.
    inline int L_texture(lua_State* L)
    {
        const char* path = luaL_checkstring(L, 1);
        wxl::lua::ui::Texture* t = wxl::lua::ui::LoadBlp(path);
        if (!t)
        {
            lua_pushnil(L);
            return 1;
        }
        PushTexture(L, t);
        return 1;
    }

    /// handle:width() -> int.
    inline int L_width(lua_State* L) { lua_pushinteger(L, CheckTexture(L, 1)->width); return 1; }

    /// handle:height() -> int.
    inline int L_height(lua_State* L) { lua_pushinteger(L, CheckTexture(L, 1)->height); return 1; }

    /// handle:size() -> w, h.
    inline int L_size(lua_State* L)
    {
        const wxl::lua::ui::Texture* t = CheckTexture(L, 1);
        lua_pushinteger(L, t->width);
        lua_pushinteger(L, t->height);
        return 2;
    }

    /// handle:id() -> number: the ImTextureID as a Lua number, usable by add_image / image.
    inline int L_id(lua_State* L)
    {
        const wxl::lua::ui::Texture* t = CheckTexture(L, 1);
        lua_pushnumber(L, static_cast<lua_Number>(static_cast<ImU64>(wxl::lua::ui::TexId(t))));
        return 1;
    }

    /// __tostring: "Texture:<path> <w>x<h>".
    inline int L_tostring(lua_State* L)
    {
        const wxl::lua::ui::Texture* t = CheckTexture(L, 1);
        lua_pushfstring(L, "Texture:%s %dx%d", t->path.c_str(), t->width, t->height);
        return 1;
    }

    /**
     * @brief Creates the "wxl.texture" handle metatable (methods under __index, __tostring, locked
     *        __metatable, no __gc). Call once per state at Start(), before Register().
     * @param L  the Lua state.
     */
    inline void RegisterTextureMeta(lua_State* L)
    {
        luaL_newmetatable(L, kTextureMeta);                 // [mt]
        lua_pushcfunction(L, &L_tostring); lua_setfield(L, -2, "__tostring");
        lua_pushboolean(L, 0);             lua_setfield(L, -2, "__metatable"); // getmetatable() sees false
        lua_newtable(L);                                    // [mt, methods]
        lua_pushcfunction(L, &L_width);  lua_setfield(L, -2, "width");
        lua_pushcfunction(L, &L_height); lua_setfield(L, -2, "height");
        lua_pushcfunction(L, &L_size);   lua_setfield(L, -2, "size");
        lua_pushcfunction(L, &L_id);     lua_setfield(L, -2, "id");
        lua_setfield(L, -2, "__index");                     // mt.__index = methods
        lua_pop(L, 1);                                      // []
    }

    /**
     * @brief Adds the callable `wxl.texture` to the table on top of the stack. Stack-neutral.
     * @param L  the engine lua_State, with the target `wxl` table at index -1.
     */
    inline void Register(lua_State* L)
    {
        lua_pushcfunction(L, &L_texture); lua_setfield(L, -2, "texture");
    }
}
