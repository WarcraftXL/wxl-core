// Named hook points: the addresses behind WXL_Api::HookAttachByName, so an extension can attach to
// one without including an offsets/ header itself.
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

#include "engine/hook/Hook.hpp"

#include <cstddef>

/// The hook points of HookPoints.def, attached by name. The core's own detours go through Attach,
/// extensions through WXL_Api::HookAttachByName; both land in the same per-address chain.
namespace wxl::runtime::hookpoints
{
    /**
     * Installs a detour on a named hook point.
     *
     * @param string pointName : a name from HookPoints.def
     * @param void* detour
     * @param void** original : receives the next link in the chain
     * @param int32 priority : chain position, lower runs first
     * @return int32 ok : non-zero when registered, zero for an unknown name
     */
    int AttachByName(const char* pointName, void* detour, void** original, int priority);

    /**
     * Installs a detour on a named hook point; the detour and the trampoline share one function
     * type, so a mismatch does not compile.
     *
     * @param string pointName : a name from HookPoints.def
     * @param Fn* detour
     * @param Fn** original : receives the next link in the chain
     * @param int32 priority = 0 : chain position, lower runs first
     * @return bool ok
     */
    template <class Fn>
    inline bool Attach(const char* pointName, Fn* detour, Fn** original, int priority = wxl::hook::kDefaultPriority)
    {
        return AttachByName(pointName, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original), priority) != 0;
    }

    /// The number of hook points in the table.
    size_t Count();
}
