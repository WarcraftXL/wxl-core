// GUID-backed object proxies: the userdata + metatable machinery behind wxl Unit objects.
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

struct lua_State;

/// A Lua-side object (an `wxl` Unit, later a Player, GameObject…) is a userdata that stores only a
/// 64-bit GUID plus a type tag — never a native pointer. The pointer is re-resolved from the GUID
/// on every access (ResolveObject), so a proxy that outlives its object simply stops resolving
/// instead of dangling. Identity is interned: PushUnit returns the SAME userdata for a GUID for as
/// long as Lua keeps a reference, via a weak-valued cache in the registry, so `a == b` and rawequal
/// both hold and handlers can key tables by unit. Every native read here is SEH-guarded so a race
/// with world teardown yields nil/false/nullptr instead of faulting the game thread.
namespace wxl::lua
{
    /// Registry name of the Unit metatable; also the luaL_checkudata type tag for a Unit userdata.
    inline constexpr char kUnitMeta[] = "wxl.Unit";

    /// The tag a proxy carries. Players are units at the native layer (IsPlayer is decided at
    /// resolve time by type mask); the tag exists to distinguish future non-unit families.
    enum class ObjectType : uint32_t
    {
        Unit,
        Player,
        GameObject,
    };

    /// The userdata payload. POD by design: no owning pointer, nothing to unwind.
    struct ObjectProxy
    {
        uint64_t   guid;
        ObjectType type;
    };

    /**
     * @brief Creates the Unit metatable and the weak proxy cache in a fresh state's registry.
     *        Call once per state at Start(), BEFORE the method contexts populate the metatable.
     * @param L  the Lua state.
     */
    void RegisterMetatables(lua_State* L);

    /**
     * @brief Pushes the interned Unit userdata for a GUID (nil when guid == 0). Reuses the cached
     *        userdata for a live GUID so proxy identity is stable.
     * @param L     the Lua state.
     * @param guid  the object GUID.
     */
    void PushUnit(lua_State* L, uint64_t guid);

    /**
     * @brief Reads the GUID from a Unit userdata at a stack index, raising a typed error if the
     *        value is not a wxl Unit.
     * @param L    the Lua state.
     * @param idx  stack index of the expected Unit.
     * @return the stored GUID.
     */
    uint64_t CheckGuid(lua_State* L, int idx);

    // --- SEH-guarded native access (return nullptr/0/false on a fault or a miss) ---

    /**
     * @brief Re-resolves a GUID to a native object via the client's object-by-GUID entry.
     * @param guid      the object GUID (0 resolves to nullptr).
     * @param typemask  the accepted object type mask (unit, player…).
     * @return the native object pointer, or nullptr if absent or on fault.
     */
    void* ResolveObject(uint64_t guid, uint32_t typemask);

    /** @brief Resolves a GUID as a unit-or-player. Convenience over ResolveObject. */
    void* ResolveUnit(uint64_t guid);

    /** @brief Reads a u64 GUID from a client global address (0 on fault). */
    uint64_t ReadGuidAt(uintptr_t addr);

    /** @brief Calls the client's active-player-GUID entry (0 on fault). */
    uint64_t ActivePlayerGuid();

    /** @brief Reads a unit's world position into out[3] (false on fault or null unit). */
    bool ReadPosition(void* unit, float out[3]);

    /** @brief Reads self's reaction toward other into *out (false on fault or null operand). */
    bool ReadReaction(void* self, void* other, int* out);

    /**
     * @brief Projects a world position to ImGui pixel space via the live camera view-projection and
     *        the live render viewport. Shared by wxl.camera.world_to_screen and unit:GetScreenPosition
     *        so both use one convention. SEH-guarded (matrix + device viewport reads).
     *
     * The transform is the row-vector convention clip = [x,y,z,1] * ViewProj, a perspective divide by
     * clip.w, then an NDC(-1..1)->pixel map with a top-left origin (y flipped) sized to the viewport.
     *
     * @param pos      the world position, pos[0..2].
     * @param sx       receives the screen x in pixels (still written when offscreen/behind).
     * @param sy       receives the screen y in pixels (still written when offscreen/behind).
     * @param visible  receives false when the point is behind the camera (clip.w <= 0) or falls
     *                 outside [0,viewport]; the cull signal for callers.
     * @return true when the matrix/viewport were readable (sx/sy/visible valid), false on fault or
     *         when the graphics device / viewport is unavailable.
     */
    bool WorldToScreenPixels(const float pos[3], float& sx, float& sy, bool& visible);
}
