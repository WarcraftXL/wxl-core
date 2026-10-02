// CLayoutFrame for the target client (335): the scale a region is laid out with, the fields around
// it, and the one entry that writes it.
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

// CLayoutFrame is the base every frame AND every region shares, and it already carries a scale that
// the layout math multiplies into both the object's size and its anchor offsets. What the era lacks
// is a way for a region to have a scale of its own -- not the machinery to lay one out.
namespace wxl::offsets::engine::layout
{
    // Distance from a FrameScript_Object base to its CLayoutFrame subobject. The inverse of
    // engine::xml::kRegionToScriptObject, and confirmed outright by CSimpleTexture::~CSimpleTexture
    // (0x00483010), which restores both vtable pointers: *this = 0x009EA1D8 (the script-object
    // vtable) and this[8] = 0x009EA188 (the CLayoutFrame one), this[8] being this + 0x20.
    constexpr int32_t kScriptObjectToLayoutFrame = 0x20;

    // CLayoutFrame fields, from the subobject. Width and height are the object's OWN size in layout
    // units, unscaled; kLayoutFrameScale is what the resolver multiplies them by.
    //   width   CLayoutFrame::GetWidth (0x00488D10) is the two instructions that read +0x54,
    //           and CLayoutFrame::SetWidth (0x00489F80) is D9 59 54, the one that writes it.
    //   scale   CLayoutFrame::Left (0x004892A0) computes Right - GetWidth() * this[0x17], and
    //           0x17 * 4 == 0x5C.
    constexpr uintptr_t kLayoutFrameWidth  = 0x54;
    constexpr uintptr_t kLayoutFrameHeight = 0x58;
    constexpr uintptr_t kLayoutFrameScale  = 0x5C;

    // The same scale reached from the FrameScript_Object base (0x20 + 0x5C). This is the field
    // CSimpleFrame_GetEffectiveScale (0x0049F790) pushes, so it is the EFFECTIVE scale: the product
    // of the object's own scale and every ancestor's, not the object's own.
    constexpr uintptr_t kRegionEffectiveScale = 0x7C;

    // The object's parent, from the FrameScript_Object base. CScriptRegion_SetParent walks it as
    // piVar8[0x25] to refuse a parenting loop, and CSimpleFrame::UpdateScale reads the parent's
    // effective scale as *(float*)(*(int*)(this + 0x94) + 0x7C).
    constexpr uintptr_t kRegionParent = 0x94;

    // A CSimpleFrame's OWN scale, the value Frame:SetScale stores and Frame:GetScale pushes
    // (0x0049F7D0 reads +0xB8). Regions have no such field, which is the whole of the gap.
    constexpr uintptr_t kFrameOwnScale = 0xB8;

    // CLayoutFrame::SetLayoutScale(scale, force). __thiscall on the CLayoutFrame subobject, two stack
    // arguments, callee-cleaned: 0x00489E90 opens 55 8B EC and reads D9 45 08 (the float at [ebp+8])
    // and 80 7D 0C 00 (the force byte at [ebp+0xC]), writes D9 59 5C (this + 0x5C), then clears the
    // cached rect at this + 0x44..0x50 and the resolved bit in this + 0x40 before calling
    // CLayoutFrame::Resize -- so nothing further is needed to make a new scale take effect. Every
    // exit is 5D C2 08 00 with the result in AL: 1 when the scale changed, 0 when it did not.
    //
    // force == 0 makes it a no-op when the value is unchanged; that is what CSimpleFrame::UpdateScale
    // passes on the era's own path.
    constexpr uintptr_t kLayoutFrameSetScale = 0x00489E90;
    using LayoutFrameSetScaleFn = char(__fastcall*)(void* layoutFrame, void* edx, float scale,
                                                    int force);

    // Slot 0x14 of the CLayoutFrame vtable, and the ONE point at which a region's scale is written
    // from outside itself. CSimpleFrame::UpdateScale (0x004915A0) walks the frame's region list and
    // dispatches it per region -- at 0x00491618: 8B 43 20 (the region's CLayoutFrame vptr) /
    // 8B 40 14 (this slot) / 8D 4B 20 (ecx = region + 0x20) / push force / push+fstp the scale.
    // CSimpleTexture does not override it: 0x009EA188 + 0x14 holds kLayoutFrameSetScale itself.
    //
    // A frame child is reached by a different loop in the same function, through script-object vtable
    // slot 0xD0 (CSimpleFrame::UpdateScale again), which is why this slot only ever carries the
    // parent's effective scale verbatim -- a region has nothing of its own to fold into it.
    constexpr uintptr_t kLayoutFrameSetScaleSlot = 0x14;

    // The frame's region list, the one that loop walks: CSimpleFrame::RegisterRegion (0x00490640) is
    // 50 / 81 C1 0C 02 00 00 / call TSList::LinkToTail, so the list lives at frame + 0x20C. Child
    // FRAMES are a different list (frame + 0x280).
    constexpr uintptr_t kFrameRegionList = 0x20C;

    // CSimpleTexture's script type id slot, claimed lazily from engine::lua::kObjectTypeCounter by
    // whichever of its bindings runs first -- A1 3C 79 B4 00 opens CSimpleTexture_GetTexture
    // (0x0048C670) and its siblings. CSimpleTexture::IsA (0x00482C60) accepts this id, CScriptRegion's
    // (0x00B49978) and FrameScript_Object's (0x00B4997C) and nothing else, so handing it to
    // FrameScript_GetObjectThis is what refuses a Frame or a FontString passed as self.
    constexpr uintptr_t kSimpleTextureTypeId = 0x00B4793C;

    // CSimpleTexture::~CSimpleTexture, the scalar deleting destructor: __thiscall(this, flags), one
    // stack argument, returns `this`. Bit 0 of the flag is what makes it free the object. The single
    // point at which a table keyed by texture pointer can drop its entry before the address is
    // handed out again.
    constexpr uintptr_t kSimpleTextureDestructor = 0x00483010;
    using SimpleTextureDestructorFn = void*(__fastcall*)(void* texture, void* edx, unsigned int flags);
}
