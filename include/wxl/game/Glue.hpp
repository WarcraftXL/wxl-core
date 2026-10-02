// The glue screens' model frame: its 3D render slot, and its script methods.
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

#include <cstdint>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/engine/Gx.hpp"
#include "wxl/offsets/engine/Lua.hpp"

/**
 * @brief The model frame the login and character-select screens are built on.
 *
 * The engine has no single "3D is done" point: it defers every 3D render into a per-frame-object
 * callback, and this frame type owns the one the glue screens use. That callback is also where a
 * different scene can be put in a frame's place.
 *
 * The frame type carries script methods -- SetModel is the one the stock glue Lua calls -- and new
 * ones can be added alongside, which is how a screen says what it should show rather than a file
 * beside the binary saying it on the screen's behalf.
 */
namespace wxl::game::glue
{
    namespace off  = wxl::offsets::engine::gx;
    namespace loff = wxl::offsets::engine::lua;

    /// Entry of the glue screens' 3D render callback.
    constexpr uintptr_t kModelRender = off::kSimpleModelFFXRender;

    /// Its signature, for a detour and the matching trampoline.
    using ModelRenderFn = off::GlueModelRenderFn;

    /// Entry that registers the frame type's script methods, and the place to add more.
    constexpr uintptr_t kRegisterMethods = off::kSimpleModelRegisterMethods;

    /// Its signature, for a detour and the matching trampoline.
    using RegisterMethodsFn = off::RegisterScriptMethodsFn;

    /// A script method: the name Lua calls, and the function it reaches.
    using Method = loff::ScriptMethod;

    /// A script method's own signature.
    using MethodFn = loff::LuaCFunction;

    /**
     * @brief Adds script methods to the frame type.
     * @param target   The registration target handed to a RegisterMethods callback.
     * @param methods  Methods to add, living for the process lifetime.
     * @param count    How many.
     *
     * Only meaningful from inside such a callback, after the original has run: the target is a
     * metatable under construction and exists nowhere else.
     */
    inline void AddMethods(void* target, const Method* methods, int count)
    { Native<loff::FillScriptMethodTableFn>(loff::kFillScriptMethodTable)(target, methods, count); }

    /**
     * @brief Reads the frame a script method was invoked on.
     * @return The frame, or null when called outside a script method.
     *
     * This is the same pointer the 3D render callback receives, so what a method records about a frame
     * can be looked up when that frame draws.
     *
     * The frame type's script type id is claimed lazily by whichever of its methods runs first. An
     * added method may be that one, so it claims the id exactly as the stock methods do -- into the
     * type's own slot, from the shared counter.
     */
    inline void* MethodSelf()
    {
        if (Read<int>(off::kSimpleModelTypeId) == 0)
        {
            const int id = Read<int>(loff::kObjectTypeCounter) + 1;
            Write<int>(loff::kObjectTypeCounter, id);
            Write<int>(off::kSimpleModelTypeId, id);
        }
        return Native<loff::GetObjectThisFn>(loff::kGetObjectThis)(Read<int>(off::kSimpleModelTypeId));
    }
}
