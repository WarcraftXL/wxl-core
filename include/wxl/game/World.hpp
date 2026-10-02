// world bindings: selection GUIDs and GUID-to-object resolution.
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
#include <cstddef>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/engine/Gx.hpp"
#include "wxl/offsets/game/Unit.hpp"
#include "wxl/offsets/game/World.hpp"

/**
 * @brief Typed accessors for the current selection GUIDs, GUID-to-object resolution, and the map the
 *        terrain renderer is showing.
 */
namespace wxl::game::world
{
    namespace off  = wxl::offsets::game::unit;
    namespace woff = wxl::offsets::game::world;
    namespace goff = wxl::offsets::engine::gx;

    /**
     * @brief Renders a world frame the way the client does: update, render, and put back what changed.
     * @param frame  The world frame to render for; its active camera is what the scene is seen from.
     *
     * This is the client's own entry, not a reassembly of it. It runs the scene update -- which builds
     * the culling frustum, the viewer position and the day/night state -- then the render, then puts
     * the projection and view back and refreshes the shader system from them.
     *
     * Preferred over calling RenderScene directly. That draws, but leaves everything the draw depends
     * on to the caller, and each piece left out is invisible until nothing appears at all.
     */
    inline void RenderWorldFrame(void* frame)
    { Native<goff::WorldRenderFinalizeFn>(goff::kWorldRenderFinalize)(frame); }

    /**
     * @brief Reads the map the terrain renderer currently holds.
     * @return The map id, or -1 when no map is loaded.
     */
    inline int CurrentMapId()
    { return *reinterpret_cast<int*>(woff::kCurrentMapId); }

    /**
     * @brief Moves the point terrain streams around.
     * @param pos  World position x, y, z.
     *
     * The streaming window is centred here rather than on any object, so a scene with no player still
     * decides what to load. Set it before loading a map, or the load box lands wherever it last was.
     */
    inline void SetStreamFocus(const float pos[3])
    {
        *reinterpret_cast<float*>(woff::kFocusPosX) = pos[0];
        *reinterpret_cast<float*>(woff::kFocusPosY) = pos[1];
        *reinterpret_cast<float*>(woff::kFocusPosZ) = pos[2];
    }

    /**
     * @brief Loads a map by name, replacing whatever was loaded.
     * @param mapDir  Map directory name under World\\Maps, e.g. "Azeroth".
     * @param mapId   Numeric map id recorded as the current map.
     *
     * Purges the loaded tiles, reads that map's WDT and WDL, streams the tiles around the focus point
     * and drains the pending reads before returning, so a fixed camera needs no further streaming. It
     * moves no player and needs no server session.
     */
    inline void EnterMap(const char* mapDir, int mapId)
    { Native<woff::World_MapEnterFn>(woff::kMapEnter)(mapDir, mapId); }

    /**
     * @brief Sets how far the world is drawn.
     * @param yards  Requested distance; the engine clamps it to what the loaded map allows.
     *
     * Reads the loaded map, so it belongs after EnterMap rather than before. The scene culls against
     * this and the horizon fades to fog over it, so leaving it unset draws nothing at all -- and an
     * empty frame is the fog colour, which looks like a scene that is merely too foggy.
     *
     * A projection built alongside should use the same distance, or what is culled and what is drawn
     * disagree at the edges.
     */
    inline void SetFarClip(float yards)
    { Native<woff::World_SetFarClipFn>(woff::kSetFarClip)(yards); }

    /// Near plane the engine pairs with the far clip above.
    constexpr float kNearClip = 0.2f;

    /**
     * @brief Reads the per-category render switches the world passes consult.
     * @return The bitmask. Bit 1 gates the solid terrain draw.
     */
    inline uint32_t Enables()
    { return *reinterpret_cast<const uint32_t*>(woff::kEnables); }

    /**
     * @brief Rebuilds what the scene render culls and shades against.
     * @param eye     World position the scene is viewed from.
     * @param target  World position it looks at. The direction is derived from the two, so any point
     *                along the line of sight does; it is not an orientation.
     * @param focus   World position terrain streams around, usually the eye.
     *
     * RenderScene culls against the frustum this leaves and reads its viewer position from the globals
     * this writes, so calling the render without it draws nothing: an unbuilt frustum rejects
     * everything. It also advances day and night, which is what gives the scene its light.
     */
    inline void PrepareScene(const float eye[3], const float target[3], const float focus[3])
    { Native<woff::World_PrepareUpdateFn>(woff::kPrepareUpdate)(eye, target, focus); }

    /**
     * @brief Draws the terrain, map objects and doodads around a viewer.
     * @param viewerPos  World position x, y, z the scene is drawn around.
     * @param flags      Render flags; 0 is the neutral value.
     *
     * Reads the camera matrices from the engine globals (see wxl::game::camera), so set those first.
     */
    inline void RenderScene(const float viewerPos[3], uint32_t flags)
    { Native<woff::World_RenderFn>(woff::kRender)(viewerPos, flags); }

    /**
     * @brief Reads the GUID of the unit under the cursor.
     * @return The mouseover GUID.
     */
    inline unsigned long long MouseoverGuid()
    { return *reinterpret_cast<unsigned long long*>(off::kMouseoverGuid); }

    /**
     * @brief Reads the GUID of the current target.
     * @return The target GUID.
     */
    inline unsigned long long TargetGuid()
    { return *reinterpret_cast<unsigned long long*>(off::kTargetGuid); }

    /**
     * @brief Reads the GUID of the active player.
     * @return The active player GUID.
     */
    inline unsigned long long ActivePlayerGuid()
    { return Native<off::ActivePlayerGuidFn>(off::kActivePlayerGuid)(); }

    /**
     * @brief Resolves a GUID to an object filtered by type mask.
     * @param guid      Object GUID.
     * @param typeMask  Accepted object type mask.
     * @return The object, or null when not found or filtered out.
     */
    inline void* ResolveObject(unsigned long long guid, unsigned typeMask)
    { return Native<off::GetObjectFn>(off::kGetObjectByGuid)(guid, typeMask, "wxl", 0); }

    /**
     * @brief Reads a unit's world position.
     * @param unit  Unit object from GetObject.
     * @param out   Receives x, y, z in out[0..2].
     */
    inline void UnitPosition(void* unit, float out[3])
    {
        const float* p = static_cast<off::UnitObject*>(unit)->position;
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
    }

    constexpr unsigned kTypeMaskObject        = off::kTypeMaskObject;
    constexpr unsigned kTypeMaskItem          = off::kTypeMaskItem;
    constexpr unsigned kTypeMaskContainer     = off::kTypeMaskContainer;
    constexpr unsigned kTypeMaskUnit          = off::kTypeMaskUnit;
    constexpr unsigned kTypeMaskPlayer        = off::kTypeMaskPlayer;
    constexpr unsigned kTypeMaskGameObject    = off::kTypeMaskGameObject;
    constexpr unsigned kTypeMaskDynamicObject = off::kTypeMaskDynamicObject;
    constexpr unsigned kTypeMaskCorpse        = off::kTypeMaskCorpse;

    namespace detail
    {
        /** @brief Reads one slot out of an object's virtual table as a callable. */
        template <class Fn>
        inline Fn Virtual(void* obj, size_t slot)
        { return Fn((*static_cast<void***>(obj))[slot]); }
    }

    /**
     * @brief Reads an object's GUID out of the header every object carries.
     * @param obj  Any object.
     * @return The GUID, or 0 when obj is null.
     */
    inline unsigned long long Guid(void* obj)
    { return obj ? static_cast<off::ObjectBase*>(obj)->header->guid : 0ull; }

    /**
     * @brief Reads which categories an object belongs to.
     * @param obj  Any object.
     * @return The type mask, or 0 when obj is null. Test it against a kTypeMask* value.
     */
    inline unsigned TypeMask(void* obj)
    { return obj ? static_cast<off::ObjectBase*>(obj)->header->typeMask : 0u; }

    /**
     * @brief Reads any object's world position, whatever its concrete type.
     * @param obj  Any object.
     * @param out  Receives x, y, z in out[0..2]; the origin when obj is null.
     *
     * A type that does not implement this reports the origin, which is indistinguishable from
     * genuinely standing there.
     */
    inline void Position(void* obj, float out[3])
    {
        out[0] = out[1] = out[2] = 0.0f;
        if (obj) detail::Virtual<off::PositionFn>(obj, off::kVtPosition)(obj, out);
    }

    /**
     * @brief Reads which way an object is turned.
     * @param obj  Any object.
     * @return Orientation in radians counter-clockwise from +X, or zero when obj is null.
     *
     * The client's own callers pass this straight to fsincos and read the results as x and y, so a
     * heading built from it is (cos, sin) and agrees with atan2(y, x) in both sign and zero point.
     *
     * A type that does not implement the slot reports whatever its stub reports, which is
     * indistinguishable from genuinely facing east -- the same caveat Position carries about the
     * origin.
     */
    inline float Facing(void* obj)
    {
        return obj ? detail::Virtual<off::FacingFn>(obj, off::kVtFacing)(obj) : 0.0f;
    }

    /**
     * @brief Reads the anchor above an object where the client hangs its name.
     * @param obj  Any object.
     * @param out  Receives x, y, z in out[0..2]; the origin when obj is null.
     */
    inline void NamePosition(void* obj, float out[3])
    {
        out[0] = out[1] = out[2] = 0.0f;
        if (obj) detail::Virtual<off::PositionFn>(obj, off::kVtNamePosition)(obj, out);
    }

    // The walk reads the object manager out of thread-local storage and dereferences it without
    // checking, so anywhere there is no world session it faults rather than finding nothing. Detour
    // this to answer for such a place; the render path enumerates objects whether or not any exist.

    /// Entry that walks the resident objects.
    constexpr uintptr_t kEnumObjectsSeam = off::kEnumObjects;

    /// Its signature, for a detour and the matching trampoline.
    using EnumObjectsFn = off::EnumObjectsFn;

    /// The per-object callback it invokes. Returning zero stops the walk.
    using EnumStepFn = off::EnumStepFn;

    /**
     * @brief Calls fn once per resident object, with its GUID.
     * @param fn  Invoked per object; returning false stops the walk early.
     *
     * Main thread only. Resolving each GUID is the caller's business, which is what makes the typed
     * overload below the one to reach for.
     */
    template <class Fn>
    void ForEachObject(Fn fn)
    {
        struct Step
        {
            static int __cdecl Visit(uint32_t low, uint32_t high, void* user)
            { return (*static_cast<Fn*>(user))((static_cast<unsigned long long>(high) << 32) | low) ? 1 : 0; }
        };
        Native<off::EnumObjectsFn>(off::kEnumObjects)(&Step::Visit, &fn);
    }

    /**
     * @brief Calls fn once per resident object of the given categories, with the object itself.
     * @param typeMask  Accepted categories, one or more kTypeMask* values combined.
     * @param fn        Invoked as fn(guid, object); returning false stops the walk early.
     *
     * Objects outside typeMask are skipped without reaching fn: the lookup is the filter.
     */
    template <class Fn>
    void ForEachObject(unsigned typeMask, Fn fn)
    {
        ForEachObject([&](unsigned long long guid) {
            void* obj = ResolveObject(guid, typeMask);
            return obj ? fn(guid, obj) : true;
        });
    }
}
