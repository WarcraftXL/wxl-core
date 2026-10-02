// The scale a region is laid out with: reading it, writing it, and the two seams that carry one
// added from outside.
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
#include "wxl/offsets/engine/Layout.hpp"
#include "wxl/offsets/engine/Lua.hpp"

/**
 * @brief CLayoutFrame, the base a frame and a region share, seen through its scale.
 *
 * The era already lays a region out at size * scale with its anchor offsets scaled to match --
 * CLayoutFrame::Left is Right - GetWidth() * scale and the anchor resolver multiplies each stored
 * offset by the same field. What it lacks is any way for a REGION to hold a scale of its own: only
 * CSimpleFrame has an own-scale field, and a region's copy of the shared one is overwritten with the
 * parent's effective scale whenever the parent's changes.
 *
 * So the gap is a place to keep the region's own factor and a point at which to fold it back in, not
 * any part of the layout math.
 */
namespace wxl::game::layout
{
    namespace off = wxl::offsets::engine::layout;

    /// The entry that writes a CLayoutFrame's scale, and the vtable slot a region is reached through.
    constexpr uintptr_t kSetScale     = off::kLayoutFrameSetScale;
    constexpr uintptr_t kSetScaleSlot = off::kLayoutFrameSetScaleSlot;

    /// Its signature, for a detour and the matching trampoline. The `this` is the CLayoutFrame
    /// SUBOBJECT, not the FrameScript_Object base; use LayoutFrameOf to take the step.
    using SetScaleFn = off::LayoutFrameSetScaleFn;

    /// CSimpleTexture's scalar deleting destructor and its signature, for the detour that drops a
    /// texture's entry from a side table before its address is handed out again.
    constexpr uintptr_t kTextureDestructor = off::kSimpleTextureDestructor;
    using TextureDestructorFn = off::SimpleTextureDestructorFn;

    /// The CLayoutFrame subobject of a FrameScript_Object base.
    inline void* LayoutFrameOf(void* object)
    { return static_cast<char*>(object) + off::kScriptObjectToLayoutFrame; }

    /// The FrameScript_Object base a CLayoutFrame subobject belongs to.
    inline void* ObjectOf(void* layoutFrame)
    { return static_cast<char*>(layoutFrame) - off::kScriptObjectToLayoutFrame; }

    /**
     * @brief The scale the object is actually laid out at.
     * @return The product of its own scale and every ancestor's, as the client last computed it.
     *
     * The same field Frame:GetEffectiveScale pushes, read the same way, so a region and a frame
     * answer from one place.
     */
    inline float EffectiveScale(void* object)
    {
        return *reinterpret_cast<const float*>(static_cast<char*>(object) +
                                               off::kRegionEffectiveScale);
    }

    /// The object's parent, or null. A region's parent is always a frame.
    inline void* Parent(void* object)
    {
        return *reinterpret_cast<void* const*>(static_cast<char*>(object) + off::kRegionParent);
    }

    /// The effective scale a child of @p object inherits; 1 for an object with no parent.
    inline float ParentEffectiveScale(void* object)
    {
        void* parent = Parent(object);
        return parent ? EffectiveScale(parent) : 1.0f;
    }

    /**
     * @brief Writes an object's laid-out scale through the engine's own entry.
     * @param entry   The entry to call: the trampoline when the writer is detoured, kSetScale when
     *                it is not. Passing the detoured address back in would re-enter the detour.
     * @param object  FrameScript_Object base.
     * @param scale   The absolute scale to lay the object out at.
     * @param force   Non-zero applies the value even when it equals the current one.
     * @return true when the scale changed, which is also when a relayout was queued.
     *
     * The entry invalidates the object's cached rect and queues its resize itself, so nothing further
     * is needed to make the new value take effect.
     */
    inline bool SetScaleThrough(SetScaleFn entry, void* object, float scale, int force)
    {
        return entry(LayoutFrameOf(object), nullptr, scale, force) != 0;
    }

    /// CSimpleTexture's script type id slot, for FrameScript_GetObjectThis. Claimed lazily from the
    /// shared counter by whichever texture binding runs first, so a binding added to the class has to
    /// claim it the same way and into the same slot.
    inline int TextureTypeId()
    {
        int* slot = reinterpret_cast<int*>(off::kSimpleTextureTypeId);
        if (*slot == 0)
        {
            int* counter = reinterpret_cast<int*>(wxl::offsets::engine::lua::kObjectTypeCounter);
            *slot        = ++(*counter);
        }
        return *slot;
    }

    /**
     * @brief The texture a script method was invoked on.
     * @return The texture's FrameScript_Object base.
     *
     * DOES NOT RETURN for a self that is not a texture: the client's resolver raises a script error,
     * and CSimpleTexture::IsA accepts only the texture, region and script-object type ids, so a Frame
     * or a FontString passed as self is refused there rather than reaching a field write.
     */
    inline void* TextureSelf()
    {
        using GetObjectThisFn = wxl::offsets::engine::lua::GetObjectThisFn;
        return Native<GetObjectThisFn>(wxl::offsets::engine::lua::kGetObjectThis)(TextureTypeId());
    }
}
