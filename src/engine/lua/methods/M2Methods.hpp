// M2 method context: the wxl.m2 subtable reading a model/instance event pointer's identity fields.
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
#include "engine/lua/Marshal.hpp"
#include "engine/lua/methods/PointerArg.hpp"
#include "offsets/game/M2.hpp"

/// The M2 context, in the CameraMethods mould: Register() adds the wxl.m2 subtable to the `wxl` table on
/// the stack. The model fields take the runtime-model pointer a "model_load" / "model_load_pre" /
/// "m2_skin_finalize" handler received as LIGHTUSERDATA; instance_model takes the render-context pointer a
/// "m2_update" / "bone_palette" handler received. Each field is an SEH-guarded POD read off a confirmed
/// M2 object offset, so a stale or wrong-typed event pointer yields nil rather than faulting. Header-only
/// and inline.
namespace wxl::lua::methods::m2
{
    namespace off = wxl::offsets::game::m2;

    /// The model path-stem inline buffer is bounded by the client; cap our copy conservatively.
    inline constexpr int kStemCap = 256;

    /// wxl.m2.path_stem(model) -> string or nil (inline path stem, no extension). model is a model_load /
    /// model_load_pre / m2_skin_finalize event lightuserdata.
    inline int L_pathStem(lua_State* L)
    {
        void* m = CheckPtr(L, 1);
        char buf[kStemCap];
        if (!m || SehReadCStr(m, off::kOffModelPathStem, buf, kStemCap) == 0)
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<const char*>(buf));
        return 1;
    }

    /// wxl.m2.file_size(model) -> int or nil (byte size of the parsed .m2 buffer). model is a model_load
    /// event lightuserdata.
    inline int L_fileSize(lua_State* L)
    {
        void* m = CheckPtr(L, 1);
        uint32_t v = 0;
        if (!m || !SehReadU32(m, off::kOffModelFileSize, v))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<lua_Integer>(v));
        return 1;
    }

    /// wxl.m2.flags(model) -> int or nil (runtime model flags; bit 2 selects the sibling-file open flag).
    /// model is a model_load event lightuserdata.
    inline int L_flags(lua_State* L)
    {
        void* m = CheckPtr(L, 1);
        uint32_t v = 0;
        if (!m || !SehReadU32(m, off::kOffModelFlags, v))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<lua_Integer>(v));
        return 1;
    }

    /// wxl.m2.instance_model(render_ctx) -> lightuserdata or nil: the runtime model an instance draws
    /// (render_ctx+kOffInstModel). render_ctx is an m2_update / bone_palette event lightuserdata; feed the
    /// result back into wxl.m2.path_stem / file_size / flags.
    inline int L_instanceModel(lua_State* L)
    {
        void* inst = CheckPtr(L, 1);
        void* model = inst ? SehReadPtr(inst, off::kOffInstModel) : nullptr;
        if (!model)
        {
            PushNil(L);
            return 1;
        }
        lua_pushlightuserdata(L, model);
        return 1;
    }

    /**
     * @brief Adds the wxl.m2 subtable to the table on top of the stack. Stack-neutral.
     * @param L  the engine lua_State, with the target `wxl` table at index -1.
     */
    inline void Register(lua_State* L)
    {
        static const luaL_Reg fns[] = {
            { "path_stem",      L_pathStem },
            { "file_size",      L_fileSize },
            { "flags",          L_flags },
            { "instance_model", L_instanceModel },
            { nullptr, nullptr },
        };
        RegisterModule(L, "m2", fns);
    }
}
