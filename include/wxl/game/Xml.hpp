// Reading a frame's XML node while it loads, and reaching the Lua object the client gave it.
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
#include "wxl/game/Script.hpp"
#include "wxl/offsets/engine/Xml.hpp"

/**
 * @brief The XML load path, as a detour sees it.
 *
 * The parser does not reject what it does not know: an attribute nobody reads is parsed, stored on
 * the node, and never looked at. So teaching the client a new attribute or element is adding a read,
 * never making the parser accept one -- attach after a type's LoadXML and read the node yourself.
 *
 * Every frame and region type funnels through one such entry, CScriptRegion::LoadXML, which is also
 * where the client's own parentKey lives. An object's Lua table is reachable from there: the
 * FrameScript_Object base sits a fixed distance behind the region pointer LoadXML is invoked on, and
 * carries a LUA_REGISTRYINDEX reference to the table Lua sees as the frame.
 *
 * An <AnimationGroup> and an <Animation> are outside that funnel -- they are not regions -- and each
 * has its own entry, kAnimGroupLoadXML and kAnimLoadXML. Their LoadXML is invoked on the
 * FrameScript_Object base itself, so PushScriptObject takes the `this` unshifted.
 */
namespace wxl::game::xml
{
    namespace off  = wxl::offsets::engine::xml;
    namespace loff = wxl::offsets::engine::lua;

    /// A parsed XML node. The parser's own type; only the three fields below are known.
    using Node = void*;

    /// Signature of the load entry every region and frame type reaches, for a detour and trampoline.
    using RegionLoadXMLFn = off::RegionLoadXMLFn;

    /// Its address.
    constexpr uintptr_t kRegionLoadXML = off::kScriptRegionLoadXML;

    /// Signature of the three <Scripts> load entries, and their addresses. One per family of
    /// script-bearing object; a reader of a script-handler attribute has to attach to all three.
    using ScriptsLoadXMLFn = off::ScriptsLoadXMLFn;
    constexpr uintptr_t kFrameLoadXMLScripts     = off::kSimpleFrameLoadXMLScripts;
    constexpr uintptr_t kAnimLoadXMLScripts      = off::kSimpleAnimLoadXMLScripts;
    constexpr uintptr_t kAnimGroupLoadXMLScripts = off::kAnimGroupLoadXMLScripts;

    /// Signature and the two addresses of the animation load entries. An <AnimationGroup> and an
    /// <Animation> are not regions and never reach kRegionLoadXML, so anything read for every region
    /// has to be read again at these two. CSimpleAnim::LoadXML covers every <Animation> subclass.
    using AnimLoadXMLFn = off::AnimLoadXMLFn;
    constexpr uintptr_t kAnimGroupLoadXML = off::kSimpleAnimGroupLoadXML;
    constexpr uintptr_t kAnimLoadXML      = off::kSimpleAnimLoadXML;

    /// Signature and the five addresses of the animation apply entries -- vtable slot 0x2C of
    /// <Translation>, <Rotation>, <Scale>, <Alpha> and <Path>, in that order. Every one of them
    /// reads the object to act on as anim->group->target; nothing on the animation itself says what
    /// it animates, so retargeting one animation means standing group->target somewhere else for the
    /// length of its apply.
    using AnimApplyFn = off::AnimApplyFn;
    constexpr uintptr_t kTranslationAnimApply = off::kSimpleTranslationAnimApply;
    constexpr uintptr_t kRotationAnimApply    = off::kSimpleRotationAnimApply;
    constexpr uintptr_t kScaleAnimApply       = off::kSimpleScaleAnimApply;
    constexpr uintptr_t kAlphaAnimApply       = off::kSimpleAlphaAnimApply;
    constexpr uintptr_t kPathAnimApply        = off::kSimplePathAnimApply;

    /// Signature and address of the base animation destructor every animation type ends in.
    using AnimDestructorFn = off::AnimDestructorFn;
    constexpr uintptr_t kAnimDestructor = off::kSimpleAnimDestructor;

    /**
     * @brief The slot holding what an animation's group animates.
     * @param anim  The `this` an animation load or apply entry received -- the FrameScript_Object
     *              base, animations not being regions.
     * @return Address of group->target, or null when the animation is not in a group. The value in
     *         it is the FrameScript_Object base of the animated region, the same kind of pointer
     *         PushScriptObject takes, NOT a region subobject.
     *
     * Writable on purpose: it is the only place an animation's effect can be pointed elsewhere.
     */
    inline void** AnimTargetSlot(void* anim)
    {
        if (!anim) return nullptr;

        void* group = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(anim) + off::kAnimGroupField);
        if (!group) return nullptr;

        return reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(group) + off::kAnimGroupTargetField);
    }

    /// Signature and address of the script binding "Play" reaches on an <AnimationGroup>. It reads
    /// the script state and nothing else off the stack, so retail's `group:Play(true)` arrives with
    /// its reverse flag unread; a detour here is where that argument can be given a meaning.
    using AnimGroupPlayBindingFn = off::AnimGroupPlayBindingFn;
    constexpr uintptr_t kAnimGroupPlayBinding = off::kAnimGroupPlayBinding;

    /**
     * @brief Reads the animation group a script method was invoked on.
     * @return The group, for a call from an animation-group method.
     *
     * Does NOT return for anything else: the client's own resolver raises a script error for a self
     * that is not an animation group, which is the same refusal the stock bindings give.
     *
     * The class claims its script type id lazily, from a counter shared with every other script
     * class, and whichever of its bindings runs first is what claims it. A detour standing in front
     * of one of them may be that first caller, so it claims the id exactly as the stock bindings do.
     */
    inline void* AnimGroupSelf()
    {
        int* typeId = reinterpret_cast<int*>(off::kSimpleAnimGroupTypeId);
        if (*typeId == 0)
        {
            int* counter = reinterpret_cast<int*>(loff::kObjectTypeCounter);
            *typeId = ++(*counter);
        }
        return Native<loff::GetObjectThisFn>(loff::kGetObjectThis)(*typeId);
    }

    /// Whether a group is mid-play, paused included: its play-state byte is 0 only when stopped.
    inline bool AnimGroupIsPlaying(void* group)
    {
        if (!group) return false;
        return *(reinterpret_cast<uint8_t*>(group) + off::kAnimGroupPlayStateField) != 0;
    }

    /**
     * @brief Plays a group backwards -- every animation from its `to` state to its `from` state.
     * @param group  The group, as AnimGroupSelf returns it.
     *
     * The era HAS reverse playback; what it has no way to do is ask for it. CSimpleAnimGroup::Play
     * picks its direction from the order cursor it is entered with -- below zero it plays forwards
     * from the first order, at or past the end it plays backwards from the last -- and nothing in
     * the binary ever leaves the cursor in the second state, so the branch is unreachable from Lua.
     * Standing the cursor at the order count and calling Play is that branch's entry: from there the
     * client does the whole of the work itself, copying the backwards state into every animation of
     * the group and starting the last order's reverse-sorted list.
     *
     * Reverse is then a property of the PROGRESS, not of the values: CSimpleAnim::UpdateProgress
     * hands each apply entry `1 - t` instead of `t`, before the animation's curve object, so a
     * <Scale> runs from toScale to fromScale, an <Alpha> from toAlpha to fromAlpha, and a <Path> --
     * whose apply evaluates one position from one parameter, the control-point offsets being
     * absolute displacements from the region's anchored place rather than steps -- walks its
     * polyline from the last control point back to (0, 0).
     *
     * A group already playing is STOPPED FIRST, with `requested` false. Play on a live group is a
     * no-op in this era, so without that a second call inside one play would do nothing at all;
     * stopping is also what unregisters the group from its object, so the Play that follows
     * registers it exactly once, and it is what puts every animation back to un-started. The cost is
     * an OnStop handler seeing a restart, which retail's own restart does not fire.
     *
     * The cursor is RESTORED when nothing consumed it. Play has two refusals that return before the
     * direction branch -- an empty animation list, and a group already playing and not paused, which
     * is reachable re-entrantly from a handler running inside the client's own stop -- and each would
     * otherwise leave the cursor standing at the order count with no play behind it. A later forward
     * Play reads such a cursor as neither end of the array, takes no branch, and leaves the group
     * started and never advanced. Finding the cursor still at the order count afterwards is the exact
     * test for that: the backwards branch moves it to orderCount - 1, and a handler that stopped the
     * group from inside OnPlay has moved it to -1.
     *
     * Play has one further early branch, on the finished flag 0x10 of the group's flag word, which
     * resets the cursor to -1 and would play FORWARDS over the write below. It is unreachable from
     * here: that flag is only ever set together with a non-zero play state, inside the window where
     * the client's own advance runs OnFinished, and the stop above clears the whole flag word.
     */
    inline void AnimGroupPlayReversed(void* group)
    {
        if (!group) return;

        if (AnimGroupIsPlaying(group))
            Native<off::AnimGroupStopFn>(off::kSimpleAnimGroupStop)(group, 0);

        uint8_t*  base        = reinterpret_cast<uint8_t*>(group);
        int*      cursor      = reinterpret_cast<int*>(base + off::kAnimGroupOrderCursorField);
        const int orderCount  = *reinterpret_cast<const int*>(base + off::kAnimGroupOrderCountField);
        const int savedCursor = *cursor;

        *cursor = orderCount;
        Native<off::AnimGroupPlayFn>(off::kSimpleAnimGroupPlay)(group);

        if (*cursor == orderCount) *cursor = savedCursor;
    }

    /// Signature and address of the <Texture> load entry.
    using TextureLoadXMLFn = off::TextureLoadXMLFn;
    constexpr uintptr_t kTextureLoadXML = off::kSimpleTextureLoadXML;

    /**
     * @brief Reads an attribute off a node.
     * @param node  The node.
     * @param name  Attribute name, matched without regard to case.
     * @return Its value, or null when the node does not carry the attribute. The string belongs to
     *         the parsed document and outlives the load, but not a UI reload -- copy anything kept.
     */
    inline const char* Attribute(Node node, const char* name)
    {
        if (!node || !name) return nullptr;
        return Native<off::NodeGetAttributeByNameFn>(off::kNodeGetAttributeByName)(node, nullptr, name);
    }

    /// Reads an attribute's text as a boolean, the way every stock boolean attribute is read.
    inline bool TextToBool(const char* value)
    {
        if (!value || !*value) return false;
        return Native<off::StringToBoolFn>(off::kStringToBool)(value) != 0;
    }

    /// Reads an attribute's text as a number, the way every stock numeric attribute is read.
    inline float TextToNumber(const char* value)
    {
        if (!value || !*value) return 0.0f;
        return Native<off::StringToFloatFn>(off::kStringToFloat)(value);
    }

    /**
     * @brief Reads a boolean attribute off a node.
     * @param node  The node.
     * @param name  Attribute name.
     * @return false when the attribute is absent or empty, so an omitted attribute is an off one.
     */
    inline bool AttributeIsTrue(Node node, const char* name)
    { return TextToBool(Attribute(node, name)); }

    /// The node's first child, or null.
    inline Node FirstChild(Node node)
    {
        if (!node) return nullptr;
        return *reinterpret_cast<Node*>(reinterpret_cast<uint8_t*>(node) + off::kNodeFirstChild);
    }

    /// The node after this one under the same parent, or null.
    inline Node NextSibling(Node node)
    {
        if (!node) return nullptr;
        return *reinterpret_cast<Node*>(reinterpret_cast<uint8_t*>(node) + off::kNodeNextSibling);
    }

    /**
     * @brief The node's text content.
     * @return The text, or null when the element is empty. For a script handler this is the inline
     *         body the client compiles.
     */
    inline const char* NodeText(Node node)
    {
        if (!node) return nullptr;
        return *reinterpret_cast<const char**>(reinterpret_cast<uint8_t*>(node) + off::kNodeText);
    }

    /// The node's element name.
    inline const char* NodeName(Node node)
    {
        if (!node) return nullptr;
        return *reinterpret_cast<const char**>(reinterpret_cast<uint8_t*>(node) + off::kNodeName);
    }

    /**
     * @brief The FrameScript_Object base behind the region pointer a LoadXML entry is invoked on.
     * @param region  The `this` a LoadXML detour received.
     *
     * LoadXML runs on a subobject that is not the start of the allocation; the base carrying the Lua
     * registration and the class vtable sits a fixed distance behind it. The client's own parentKey
     * reader takes exactly this step.
     */
    inline void* ObjectOf(void* region)
    {
        if (!region) return nullptr;
        return reinterpret_cast<uint8_t*>(region) + off::kRegionToScriptObject;
    }

    /**
     * @brief Pushes the Lua table an object is seen as, registering it if it has none yet.
     * @param state   Script state.
     * @param object  The FrameScript_Object base -- ObjectOf(region) for a region, and the `this`
     *                itself for an animation or an animation group, whose LoadXML is invoked on the
     *                base.
     * @return false when there was nothing to push, in which case the stack is untouched.
     *
     * The registration is the same one a named frame gets: the table is created, given the class's
     * metatable, and held by a reference in the registry. Registering an unnamed object here keeps
     * it alive for the process, which is what an assignment into a parent's table would do anyway.
     */
    inline bool PushScriptObject(void* state, void* object)
    {
        if (!state || !object) return false;

        const int ref = Native<off::ScriptObjectLuaRefFn>(off::kScriptObjectLuaRef)(object, nullptr);
        if (ref == 0) return false;

        script::RawGetI(state, script::kRegistryIndex, ref);
        if (script::Type(state, -1) != script::kTypeTable)
        {
            script::SetTop(state, -2);
            return false;
        }
        return true;
    }

    /// PushScriptObject for a region, whose LoadXML is invoked on a subobject rather than the base.
    /// @param region  The `this` a region LoadXML detour received.
    inline bool PushObject(void* state, void* region)
    {
        return PushScriptObject(state, ObjectOf(region));
    }

    /**
     * @brief Points a texture at a file.
     * @param region     The `this` a <Texture> LoadXML detour received.
     * @param file       Texture path, spelled the way the stock loader takes it: backslashes, no
     *                   extension.
     * @param horizTile  Repeat horizontally.
     * @param vertTile   Repeat vertically.
     * @return false when the file could not be loaded, in which case the texture is left as it was.
     */
    inline bool SetTexture(void* region, const char* file, bool horizTile, bool vertTile)
    {
        void* object = ObjectOf(region);
        if (!object || !file || !*file) return false;

        const int filter = *reinterpret_cast<const int*>(off::kTextureDefaultFilterMode);
        return Native<off::TextureSetTextureFn>(off::kSimpleTextureSetTexture)(
                   object, nullptr, file, horizTile ? 1 : 0, vertTile ? 1 : 0, filter, 0) != 0;
    }

    /**
     * @brief Sets a texture's four corners.
     * @param region  The `this` a <Texture> LoadXML detour received.
     * @param left    Left edge in texture coordinates.
     * @param right   Right edge.
     * @param top     Top edge.
     * @param bottom  Bottom edge.
     *
     * The engine stores all four corners rather than a rectangle; an axis-aligned rectangle is the
     * degenerate case written here.
     */
    inline void SetTexCoord(void* region, float left, float right, float top, float bottom)
    {
        void* object = ObjectOf(region);
        if (!object) return;

        const float corners[8] = { left, top, left, bottom, right, top, right, bottom };
        Native<off::TextureSetTexCoordFn>(off::kSimpleTextureSetTexCoord)(object, nullptr, corners);
    }

    /**
     * @brief Converts a pixel extent to the layout units a region's size is kept in.
     * @param pixels  Extent at the interface's reference width.
     *
     * The same conversion the <Size> reader applies to an <AbsDimension>, so a size set from here and
     * a size written in XML mean the same thing.
     */
    inline float PixelsToLayout(float pixels)
    {
        const float aspect = *reinterpret_cast<const float*>(off::kAspectCompensation);
        const float scale  = *reinterpret_cast<const float*>(off::kNdcToDdcWidthScale);
        if (aspect == 0.0f) return 0.0f;
        return scale * (pixels / (aspect * off::kLayoutReferenceWidth));
    }

    /**
     * @brief Sets a region's width, in layout units.
     * @param region  The `this` a LoadXML detour received.
     *
     * Dispatched through the same virtual slot the <Size> reader uses, so a type that overrides it --
     * CSimpleTexture does -- still gets its own.
     */
    inline void SetWidth(void* region, float width)
    {
        if (!region) return;

        uint8_t* vtable = *reinterpret_cast<uint8_t**>(region);
        auto fn = *reinterpret_cast<off::LayoutFrameSetExtentFn*>(vtable + off::kLayoutFrameSetWidthSlot);
        fn(region, nullptr, width);
    }

    /// Sets a region's height, in layout units. See SetWidth.
    inline void SetHeight(void* region, float height)
    {
        if (!region) return;

        uint8_t* vtable = *reinterpret_cast<uint8_t**>(region);
        auto fn = *reinterpret_cast<off::LayoutFrameSetExtentFn*>(vtable + off::kLayoutFrameSetHeightSlot);
        fn(region, nullptr, height);
    }
    /**
     * @brief The object's storage for one named script.
     * @param object  The `this` a <Scripts> load entry received.
     * @param name    Script name, matched without regard to case by the client.
     * @return Two ints -- the registry key of the installed handler, then its taint source -- or
     *         null when the object has no script by that name.
     *
     * Dispatched virtually, because the name a class answers to depends on the class: a Button
     * knows OnClick and a Frame does not. The signature the client would compile a body with is
     * asked for and discarded; it is an out-parameter the call requires, not something a caller
     * reading an attribute needs.
     */
    inline int* ScriptSlot(void* object, const char* name)
    {
        if (!object || !name || !*name) return nullptr;

        const char* signature = off::kDefaultScriptSignature;
        uint8_t*    vtable    = *reinterpret_cast<uint8_t**>(object);
        auto fn = *reinterpret_cast<off::GetScriptByNameFn*>(vtable + off::kGetScriptByNameSlot);
        return fn(object, nullptr, name, &signature);
    }

    /// The registry key of the handler a slot holds, or 0 when it holds none.
    inline int SlotHandler(const int* slot)
    {
        if (!slot) return 0;
        const int ref = slot[off::kScriptSlotRef];
        return ref > 0 ? ref : 0;
    }

    /**
     * @brief Installs a handler in a slot, releasing whatever it held.
     * @param state  Script state.
     * @param slot   The slot, from ScriptSlot.
     * @param ref    Registry key of the handler; the slot takes ownership of it.
     *
     * The taint source is cleared, which is what every install site in the binary does straight
     * after writing the key.
     */
    inline void SetSlotHandler(void* state, int* slot, int ref)
    {
        if (!slot || ref <= 0) return;

        const int previous = slot[off::kScriptSlotRef];
        if (previous > 0) script::Unref(state, script::kRegistryIndex, previous);

        slot[off::kScriptSlotRef]   = ref;
        slot[off::kScriptSlotTaint] = 0;
    }
}
