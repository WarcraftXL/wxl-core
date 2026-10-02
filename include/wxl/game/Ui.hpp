// ui bindings: show / hide the entire game interface (single engine flag).
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
#include "wxl/offsets/engine/Ui.hpp"

/**
 * @brief Toggles the interface layer via a single render-enable flag while the 3D world keeps drawing.
 *
 * Restore the interface to shown before anything that expects it, such as a reload.
 */
namespace wxl::game::ui
{
    namespace off = wxl::offsets::engine::ui;

    /**
     * @brief Reads the interface render-enable flag pointer.
     * @return The flag pointer, or null before the interface root exists.
     */
    inline int* Flag()
    {
        // kUiRootPtr is a fixed-address global slot; the deref reads the live root object pointer.
        void* root = Read<void*>(off::kUiRootPtr);
        return root ? &static_cast<off::UiRoot*>(root)->enabled : nullptr;
    }

    /**
     * @brief Reports whether the interface is shown.
     * @return True when shown, including before the root exists.
     */
    inline bool IsShown()      { int* f = Flag(); return f ? (*f != 0) : true; }
    /**
     * @brief Shows or hides the interface.
     * @param on  True to show, false to hide.
     */
    inline void Show(bool on)  { int* f = Flag(); if (f) *f = on ? 1 : 0; }
}
