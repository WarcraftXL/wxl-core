// FrameXML's XML load path for the target client (335): the node walk, the one region-wide load entry
// every frame and region type reaches, the three <Scripts> readers a handler is installed through,
// the two animation load entries that sit outside the region path, and the texture calls an atlas
// member is applied through.
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

namespace wxl::offsets::engine::xml
{
    // --- the parsed node ---------------------------------------------------------------------
    // Byte offsets into a parsed XML node, read off the child walk every LoadXML shares:
    // for (n = *(int*)(node + 0x08); n; n = *(int*)(n + 0x34)) SStrCmpI(*(char**)(n + 0x14), ...)
    // -- CSimpleFrame::LoadXML_Attributes at 0x00492F80, CSimpleFrame::LoadXML at 0x004936FE.
    constexpr uintptr_t kNodeFirstChild  = 0x08;
    constexpr uintptr_t kNodeName        = 0x14;
    constexpr uintptr_t kNodeNextSibling = 0x34;

    // XMLNode::GetAttributeByName, the lookup every attribute of every tag goes through. ecx = node,
    // the name on the stack; returns the attribute's value, or null when the node does not carry it.
    // Identified at 0x0048868A: 68 1C A8 9E 00 ("parentKey"), 8B CF (ecx = node), E8 -> 0x00814730.
    // Its body walks the node's attribute array -- count at +0x24, array at +0x28, stride 0x18, each
    // entry's name at +0x08 and value at +0x14 -- which is where the node offsets above were checked.
    constexpr uintptr_t kNodeGetAttributeByName = 0x00814730;
    using NodeGetAttributeByNameFn = const char*(__fastcall*)(void* node, void* edx, const char* name);

    // The client's own boolean-attribute reader; every "true"/"false" attribute in the XML load path
    // is put through it, so an attribute added from outside spells its booleans the same way.
    // __cdecl, one stack argument. 0x008154E0 in full: 55 / 8B EC / 56 / 8B 75 08 / 6A 00 /
    // E8 -> 0x00815400 / 83 C4 04 / 5E / 5D / C3 -- caller-cleaned, result in eax.
    constexpr uintptr_t kStringToBool = 0x008154E0;
    using StringToBoolFn = int(__cdecl*)(const char* value);

    // The client's own decimal reader, used for every numeric attribute in the XML load path.
    // __stdcall: 0x0076FB80 opens 55 / 8B EC / 83 EC 08 / 56 / 8B 75 08 and closes C2 04 00, so the
    // callee cleans its one argument. Returns in st0, and returns 0 for a null string (D9 EE, fldz).
    constexpr uintptr_t kStringToFloat = 0x0076FB80;
    using StringToFloatFn = float(__stdcall*)(const char* value);

    // XMLNode::GetChildByName: the same sibling walk, first child whose name matches, or null.
    constexpr uintptr_t kNodeGetChildByName = 0x008146F0;
    using NodeGetChildByNameFn = void*(__fastcall*)(void* node, void* edx, const char* name);

    // --- the one load entry every region and frame type reaches ------------------------------
    // CScriptRegion::LoadXML(node, status). ecx = the region subobject, node at [ebp+8], status at
    // [ebp+0xC]. Exactly three functions call it -- CSimpleTexture::LoadXML (site 0x00485FC7),
    // CSimpleFontString::LoadXML (0x00487484) and CSimpleFrame::LoadXML (0x0049341E) -- and every
    // other frame type's LoadXML (Button, StatusBar, EditBox, Model, ScrollFrame, Slider, HTML,
    // ColorSelect, MessageFrame, MessageScrollFrame, Cooldown, BlobFrame) reaches it by calling
    // CSimpleFrame::LoadXML. It is therefore the single seam covering all of them, and it is also
    // where the client's own parentKey reader lives, so anything added here runs where parentKey does.
    constexpr uintptr_t kScriptRegionLoadXML = 0x00488670;
    using RegionLoadXMLFn = void(__fastcall*)(void* region, void* edx, void* node, void* status);

    // Distance from the region subobject LoadXML is invoked on back to the FrameScript_Object base.
    // CScriptRegion::LoadXML takes the step outright at 0x004886AC: 8B 56 E0 (mov edx, [esi-0x20],
    // the base's vtable) then 83 C6 E0 (add esi, -0x20), and from there dispatches GetParent through
    // vtable slot +0x14 with ecx = esi. CSimpleTexture::LoadXML takes the same step for its name,
    // *(int*)(this - 0x20).
    constexpr int32_t kRegionToScriptObject = -0x20;

    // FrameScript_Object::GetOrRegisterLuaObjectRef: registers the object's Lua table if it has none,
    // then returns the LUA_REGISTRYINDEX reference to it. ecx = the FrameScript_Object base.
    // 0x00488380 in full: 56 / 8B F1 / 83 7E 04 00 / 75 07 / 6A 00 / E8 -> RegisterScriptObject /
    // 8B 46 08 / 5E / C3. That table is the one parentKey assigns into, so it is what Lua sees.
    constexpr uintptr_t kScriptObjectLuaRef = 0x00488380;
    using ScriptObjectLuaRefFn = int(__fastcall*)(void* object, void* edx);

    // --- <Scripts> ------------------------------------------------------------------------------
    // Three functions read a <Scripts> block, one per family of script-bearing object, and all
    // three have the same body: walk the children, look the element name up as a script of the
    // object, compile the element's text into a function, and store a registry key for it in the
    // object's slot for that script.
    //   CSimpleFrame::LoadXML_Scripts     0x0048FEF0  <Frame> and every frame subclass
    //   CSimpleAnim::LoadXML_Scripts      0x00497C30  <Animation> and its subclasses
    //   FrameScript_Object::LoadFunction  0x00498370  <AnimationGroup>
    // They are three rather than one because each was compiled into its own object file -- the
    // second and third name .\CSimpleAnim.cpp in their SMemAlloc call, the first .\CSimpleFrame.cpp
    // -- so a detour that is to cover every <Scripts> in the tree has to attach to all three.
    //
    // __thiscall(object, node, status): 0x0048FEF0 opens 55 / 8B EC / 81 EC 10 04 00 00 / 56 / 57 /
    // 8B F9, so the object arrives in ecx and the two pointers on the stack.
    constexpr uintptr_t kSimpleFrameLoadXMLScripts = 0x0048FEF0;
    constexpr uintptr_t kSimpleAnimLoadXMLScripts  = 0x00497C30;
    constexpr uintptr_t kAnimGroupLoadXMLScripts   = 0x00498370;
    using ScriptsLoadXMLFn = void(__fastcall*)(void* object, void* edx, void* node, void* status);

    // The element's text content -- the inline handler body. Each of the three loaders reads it as
    // node[6] at the top of its loop, next to the element name at node[5] (kNodeName above).
    constexpr uintptr_t kNodeText = 0x18;

    // GetScriptByName(name, signatureOut), virtual slot 3 of a script-bearing object. Returns a
    // pointer to the object's slot for that script, or null when the object has no script by that
    // name -- CSimpleModel::GetScriptByName (0x0095F420) is the shape in miniature: defer to the
    // base, then match the names this class adds and return `this` plus the field's offset.
    //
    // signatureOut is written only for scripts whose handler takes arguments, so the caller has to
    // initialise it: all three loaders set it to kDefaultScriptSignature before the call.
    // Dispatched at 0x0048FF26: 8B 17 (edx = vtable) / 8B 52 0C (slot 3) / lea+push signatureOut /
    // push name / 8B CF (ecx = object) / FF D2.
    constexpr uintptr_t kGetScriptByNameSlot = 0x0C;
    using GetScriptByNameFn = int*(__fastcall*)(void* object, void* edx, const char* name,
                                                const char** signatureOut);

    // A script slot is two ints: the registry key of the installed handler, then the taint source
    // recorded with it. CSimpleFrame_GetScript (0x0049EB70) reads slot[1] into the taint global and
    // pushes slot[0] through lua_rawgeti only when it is greater than zero; every install site in
    // the binary writes slot[1] = 0 straight after slot[0]. Slots are therefore 8 bytes apart --
    // CSimpleModel's OnUpdateModel at +0x358 and OnAnimFinished at +0x360.
    constexpr int kScriptSlotRef   = 0;
    constexpr int kScriptSlotTaint = 1;

    // The compile format the three loaders start from, at 0x009EB868, replaced by GetScriptByName
    // for every script whose handler takes arguments.
    constexpr const char* kDefaultScriptSignature = "return function(self) %s end";

    // --- <AnimationGroup> and <Animation> ---------------------------------------------------
    // The two load entries an animation object is built through. Neither reaches
    // CScriptRegion::LoadXML -- an animation is not a region -- so anything read for every region
    // has to be read again here.
    //
    //   CSimpleAnimGroup::LoadXML  0x0049A060  <AnimationGroup>
    //   CSimpleAnim::LoadXML       0x0049B810  <Animation> and its five subclasses
    //
    // Both are __thiscall(object, node, status) and both open the same way -- 0x0049A060:
    // 55 / 8B EC / 83 EC 0C / 53 / 56 / 57 / 8B 7D 08 (edi = node) / 8B D9 (ebx = this), and
    // 0x0049B810: 55 / 8B EC / 83 EC 0C / 53 / 8B 5D 08 (ebx = node) / 56 / 57 / 8B F9 (edi = this).
    //
    // CSimpleAnim::LoadXML is the single seam for every animation type, the way
    // CScriptRegion::LoadXML is for every region: its five callers are CSimpleTranslationAnim,
    // CSimpleRotationAnim, CSimpleScaleAnim, CSimplePathAnim and CSimpleAlphaAnim, each from its own
    // LoadXML, and CSimpleAnimGroup::LoadXML reaches a plain <Animation> through vtable slot 0x20,
    // which is that same function.
    //
    // inherits= is a re-entry into the same entry point rather than a separate pass:
    // CSimpleAnimGroup::LoadXML calls itself at 0x0049A0AF (E8 AC FF FF FF, target 0x0049A060) with
    // the inherited node, and CSimpleAnim::LoadXML dispatches slot 0x20 for it. Both do it BEFORE
    // reading their own node, so a detour that runs after the original sees the template's node
    // first and the inheriting node last.
    constexpr uintptr_t kSimpleAnimGroupLoadXML = 0x0049A060;
    constexpr uintptr_t kSimpleAnimLoadXML      = 0x0049B810;
    using AnimLoadXMLFn = void(__fastcall*)(void* object, void* edx, void* node, void* status);

    // For both of them the `this` IS the FrameScript_Object base, NOT a subobject 0x20 bytes in as
    // it is for a region: each reads its own registration inline as *(int*)(this + 4) and its Lua
    // table reference as *(int*)(this + 8) in its parentKey block, which is kScriptObjectLuaRef's
    // body written out. kRegionToScriptObject does not apply to them.

    // --- what an animation animates ------------------------------------------------------------
    // The era binds an animation to an object through its GROUP, not through the animation: every
    // animation type's apply entry loads this->group, tests it, and follows it to group->target
    // before dispatching a virtual on that target -- 8B 41 28 / 8B 40 30 at 0x00498043 for
    // <Translation>, 8B 41 28 / 8B 70 30 at 0x00498334 for <Alpha>, 8B 48 28 / 8B 49 30 at
    // 0x004980F5 for <Scale>, the register being whatever that entry had free. There is no
    // per-animation target field anywhere in the class, so an <Animation> that is to drive
    // something other than its group's object has to have group->target standing at the child for
    // the length of its own apply, and restored after.
    constexpr uintptr_t kAnimGroupField       = 0x28;
    constexpr uintptr_t kAnimGroupTargetField = 0x30;

    // group->target is the FrameScript_Object BASE of the animated region, NOT the region subobject
    // kRegionToScriptObject steps to. Both construction sites pass the base: CScriptRegion::LoadXML
    // enters LoadXML_Animations with LEA ECX,[ESI-0x20] at 0x00488729 and that `this` is handed on
    // to CSimpleAnimGroup::CSimpleAnimGroup, and CScriptRegion_CreateAnimationGroup (0x0049E350)
    // pushes the FrameScript_GetObjectThis result. The constructor stores it with 89 4E 30 at
    // 0x00499F6C.

    // The five apply entries, vtable slot 0x2C of each animation class, __thiscall(this, float) --
    // each closes C2 04 00, so the one float argument is callee-cleaned. The float is the animation's
    // interpolated value for this frame; CSimpleAnim::Update (0x00498D50) dispatches the slot, and
    // CSimpleAnimGroup's advance dispatches it again with 1.0 to settle a group that is not playing.
    // Addresses read out of each class's vtable at +0x2C: CSimpleTranslationAnim 0x009EBE98,
    // CSimpleRotationAnim 0x009EBECC, CSimpleScaleAnim 0x009EBF00, CSimpleAlphaAnim 0x009EBF38,
    // CSimplePathAnim 0x009EBF6C.
    constexpr uintptr_t kSimpleTranslationAnimApply = 0x00498040;
    constexpr uintptr_t kSimpleRotationAnimApply    = 0x00498090;
    constexpr uintptr_t kSimpleScaleAnimApply       = 0x004980F0;
    constexpr uintptr_t kSimpleAlphaAnimApply       = 0x00498330;
    constexpr uintptr_t kSimplePathAnimApply        = 0x00499140;
    using AnimApplyFn = void(__fastcall*)(void* anim, void* edx, float value);

    // CSimpleAnim::~CSimpleAnim, __thiscall with no arguments. Every one of the five derived
    // destructors ends in it -- CSimpleAlphaAnim's (0x004999F0) is the shape: restore the class
    // vtable pointer, call this, then free if the deleting flag is set. The single point at which a
    // table keyed by animation pointer can drop its entry before the address is reused.
    constexpr uintptr_t kSimpleAnimDestructor = 0x00498C10;
    using AnimDestructorFn = void(__fastcall*)(void* anim, void* edx);

    // --- playing an <AnimationGroup>, forwards or backwards -------------------------------------
    // Runtime rather than load, and here rather than in engine::lua because every field below is a
    // CSimpleAnimGroup field and the class is described nowhere else in this tree.
    //
    // THE SCRIPT BINDING TAKES NO ARGUMENT. "Play" in the animation-group method table
    // (0x00AC1AB8: name 0x009ED6A8 -> function 0x004A6EA0) is 55 bytes long and reads the stack
    // exactly once, 8B 75 08 for the script state; there is no lua_toboolean and no "Usage:
    // %s:Play(...)" string beside the ones its siblings carry. It claims the class type id, calls
    // FrameScript_GetObjectThis, calls CSimpleAnimGroup::Play and returns 0. Retail's reverse flag
    // at stack index 2 is therefore not ignored by anything downstream -- it is never read.
    constexpr uintptr_t kAnimGroupPlayBinding = 0x004A6EA0;
    using AnimGroupPlayBindingFn = int(__cdecl*)(void* state);

    // CSimpleAnimGroup's script type id slot, claimed lazily from engine::lua::kObjectTypeCounter by
    // whichever of its bindings runs first -- A1 DC 99 B4 00 85 C0 75 12 opens every one of them.
    constexpr uintptr_t kSimpleAnimGroupTypeId = 0x00B499DC;

    // CSimpleAnimGroup::Play(). __fastcall on the group alone, no arguments, 1 when the group is
    // playing after the call. Reached by E8 1E 3A FF FF at 0x004A6ECD with ECX from
    // FrameScript_GetObjectThis.
    //
    // It chooses a DIRECTION from the order cursor it is entered with, and that is the whole of the
    // era's reverse support:
    //   cursor < 0            -> cursor = 0, state = 1 (forwards)
    //   cursor >= orderCount  -> cursor = orderCount - 1, state = 2 (backwards)
    //   otherwise             -> a paused group resumes, state unchanged
    // It then copies the state byte into every animation of the group and starts the current order's
    // list -- the reverse-sorted list at order+4 for state 2, the forward one at order+8 for state 1.
    //
    // Two guards run before that. A group whose animation list is empty returns 0 having done
    // nothing, and a group already playing and not paused returns 1 having done nothing -- so Play on
    // a playing group is a NO-OP in this era, where retail restarts it.
    constexpr uintptr_t kSimpleAnimGroupPlay = 0x0049A8F0;
    using AnimGroupPlayFn = int(__fastcall*)(void* group);

    // CSimpleAnimGroup::Stop(requested). __thiscall, the bool pushed -- the script binding is
    // 6A 01 / 8B C8 / E8 at 0x004A6F49, so :Stop() from Lua passes true. Stops the current order's
    // animations, fires OnStop with `requested`, unregisters the group from its object, and resets
    // it to elapsed 0 / flags 0 / state 0 / cursor -1. Does nothing at all when the group is not
    // playing.
    constexpr uintptr_t kSimpleAnimGroupStop = 0x0049B0F0;
    using AnimGroupStopFn = void(__thiscall*)(void* group, int requested);

    // The three CSimpleAnimGroup fields the direction choice above is made from.
    //
    // kAnimGroupOrderCountField is the live length of the order array at +0x48, written by
    // CSimpleAnimGroup::SortAnimations (0x0049B1C0) with trailing empty orders trimmed; leading ones
    // are not, so a group whose animations are all order="1" has count 2 and an empty order 0.
    //
    // kAnimGroupOrderCursorField is that array's cursor, -1 from the constructor
    // (param_1[0x24] = 0xFFFFFFFF at 0x00499EB0) and back to -1 on every finish and every stop.
    //
    // kAnimGroupPlayStateField is 0 stopped, 1 playing forwards, 2 playing backwards. The group's
    // advance reads it to walk its orders in the matching direction, and each animation carries its
    // own copy at +0x35 which CSimpleAnim::UpdateProgress (0x004985F0) turns into the value the apply
    // entries receive: `progress = 1 - durationProgress` for 2, and the start and end delays trade
    // places with it.
    constexpr uintptr_t kAnimGroupOrderCountField  = 0x44;
    constexpr uintptr_t kAnimGroupPlayStateField   = 0x8D;
    constexpr uintptr_t kAnimGroupOrderCursorField = 0x90;

    // --- <Texture> -----------------------------------------------------------------------------
    // CSimpleTexture::LoadXML(node, status). ecx = the region subobject, node at [ebp+8], status at
    // [ebp+0xC] -- 0x00485F40: 55 / 8B EC / 83 EC 54 / 53 / 8B 5D 08 / 56 / 57 / 89 4D FC.
    constexpr uintptr_t kSimpleTextureLoadXML = 0x00485F40;
    using TextureLoadXMLFn = void(__fastcall*)(void* region, void* edx, void* node, void* status);

    // CSimpleTexture::SetTexture(file, horizTile, vertTile, filterMode, imageMode). ecx = the
    // FrameScript_Object base, NOT the region subobject: it stores the texture handle at base + 0xD4,
    // which the <Texture> loader reaches as region[0x2D] (0x2D * 4 + 0x20 == 0xD4). Returns 0 when the
    // file could not be loaded, in which case the caller leaves the texture as it was. All 22 call
    // sites in the binary pass the filter mode from the global below and an image mode of 0.
    constexpr uintptr_t kSimpleTextureSetTexture = 0x004859E0;
    using TextureSetTextureFn = int(__fastcall*)(void* object, void* edx, const char* file,
                                                int horizTile, int vertTile, int filterMode,
                                                int imageMode);

    // CSimpleTexture::SetTexCoord: eight floats, UL.x UL.y LL.x LL.y UR.x UR.y LR.x LR.y, written to
    // base + 0x140 .. base + 0x15C in that order. ecx = the FrameScript_Object base, as above. The
    // <TexCoords> reader writes the same eight fields directly and invalidates nothing, so neither
    // does this: the corners are read when the region draws.
    constexpr uintptr_t kSimpleTextureSetTexCoord = 0x00481640;
    using TextureSetTexCoordFn = void(__fastcall*)(void* object, void* edx, const float* corners);

    // Default texture filter mode. Every SetTexture call site in the binary passes this global.
    constexpr uintptr_t kTextureDefaultFilterMode = 0x00AC0F00;

    // --- size, for useAtlasSize ------------------------------------------------------------------
    // Virtual slots CLayoutFrame::LoadXML calls with the x and y of a <Size>, on the region subobject:
    // (**(code**)(*this + 0x1C))(width) then (**(code**)(*this + 0x20))(height). Dispatched through
    // the vtable rather than calling CLayoutFrame::SetWidth / SetHeight directly, because
    // CSimpleTexture overrides that pair of slots.
    constexpr uintptr_t kLayoutFrameSetWidthSlot  = 0x1C;
    constexpr uintptr_t kLayoutFrameSetHeightSlot = 0x20;
    using LayoutFrameSetExtentFn = void(__fastcall*)(void* region, void* edx, float value);

    // The pixels-to-layout-units conversion those two slots expect. LoadXML_Dimensions (0x00815740)
    // computes NDCToDDCWidth(value / (GetAspectCompensation() * 1024)) for both x and y, and both
    // helpers are a single multiply by a global: NDCToDDCWidth (0x0047C070) returns
    // *(float*)0x00AC0CB4 * value, GetAspectCompensation (0x0047BFE0) returns *(float*)0x00AC0CBC.
    // The two globals are read directly rather than the two functions called, there being nothing in
    // either function but that multiply.
    constexpr uintptr_t kNdcToDdcWidthScale   = 0x00AC0CB4;
    constexpr uintptr_t kAspectCompensation   = 0x00AC0CBC;
    constexpr float     kLayoutReferenceWidth = 1024.0f;
}
