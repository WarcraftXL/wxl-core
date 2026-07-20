// Unit / object lookup addresses and runtime object field offsets.
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

// INTERNAL to the core. The object/unit query entries and field offsets the unit/world bindings wrap.
namespace wxl::offsets::game::unit
{
    // --- selection globals (u64 GUIDs) ---
    constexpr uintptr_t kMouseoverGuid = 0x00BD07A0;
    constexpr uintptr_t kTargetGuid    = 0x00BD07B0;

    // --- entries ---
    // Resolve a GUID to an object (guid, typemask, tag, flag). tag is a debug string, flag is 0.
    constexpr uintptr_t kGetObjectByGuid = 0x004D4DB0;
    // Active player GUID ().
    constexpr uintptr_t kActivePlayerGuid = 0x004D3790;
    // Reaction of self toward other (this-in-ECX): 0..1 hostile, 2..3 neutral, 4+ friendly.
    constexpr uintptr_t kUnitReaction = 0x007251C0;

    // --- object lifecycle (server-driven) ---
    // Object update-block handler: parses a server update message, creating new objects in the object
    // manager or applying field deltas to existing GUIDs. One call per message (a batch of objects); hook
    // POST-call to observe the freshly created/updated set.
    constexpr uintptr_t kObjectUpdateHandler  = 0x004D73A0;
    // Object destroy handler: reads a GUID and an on-death flag, looks the object up, runs its destroy
    // path and removes it from the object manager. One call per despawn; hook PRE-call to read the doomed
    // object while it is still resident.
    constexpr uintptr_t kObjectDestroyHandler = 0x004D7610;
    // Both handlers: __cdecl(ctx, opcode, msg, packet); packet (arg 4) is the inbound message reader.
    using ObjectMsgHandlerFn = int(__cdecl*)(void* ctx, int opcode, int msg, void* packet);

    // --- target ---
    // Target-set script function: resolves a unit token and sets the player's current target (writes
    // kTargetGuid). __cdecl(scriptState); hook POST-call to read the applied target from kTargetGuid.
    constexpr uintptr_t kTargetSet = 0x0051A5C0;
    using TargetSetFn = int(__cdecl*)(void* scriptState);

    // --- object field offsets ---
    constexpr size_t kUnitModelField    = 0xB4;  // unit object -> body model
    constexpr size_t kModelParentField  = 0x48;  // model -> parent model (0 = root)
    constexpr size_t kUnitPositionField = 0x798; // unit object -> world position (3 floats x, y, z)

    // --- update-field commit (RE'd via live write-breakpoint tracing) ---
    // `mov dword ptr [eax+edx*4], ecx` -- the instruction that commits one uint32 update field into an
    // object's field array. eax = field array base, edx = field index, ecx = new value. This fires for
    // EVERY field on EVERY object (health, mana, auras, item slots, everything) -- filter on edx before
    // doing any work. NOT a function boundary: the true entry of the containing function was not located,
    // this is the raw instruction address. Safe to detour here specifically because it sits immediately
    // before the function's own epilogue (`pop ebp; ret 8`), so nothing branches into the overwritten
    // bytes -- this pattern should not be copied to hook other mid-function instructions without the
    // same check. Hooked via a naked/raw-register stub, not the typed Install<Fn> path (see GameHooks.cpp).
    constexpr uintptr_t kUnitFieldSetWrite = 0x00743BAC;
    // Raw instruction hook, not a real function boundary -- see the comment above. __cdecl() with no
    // params/return: the naked hook stub reads eax/ecx/edx manually rather than through the calling
    // convention, so this alias exists purely to type g_origUnitFieldSetWrite for the jmp thunk.
    using UnitFieldSetWriteFn = void(__cdecl*)();

    // Update-field indices for the three weapon-category visible-item entry fields (WotLK 3.3.5a client,
    // build matching this offsets table). Verified empirically: each fires with ecx == the newly equipped
    // item's entry ID. Believed to be PLAYER_VISIBLE_ITEM_{1,2,3}_ENTRYID; each is presumably followed by
    // a paired *_ENCHANTMENT field one index later (unconfirmed -- only the ENTRYID indices were verified).
    constexpr uint32_t kFieldVisibleItemMainhandEntry = 0x139;
    constexpr uint32_t kFieldVisibleItemOffhandEntry  = 0x13B;
    constexpr uint32_t kFieldVisibleItemRangedEntry   = 0x13D;

    // Offset from an object's base pointer to its update-field array, i.e. the `eax` value seen at
    // kUnitFieldSetWrite equals object_ptr + kUnitFieldArrayOffset. Confirmed via live diff against
    // the local player's own object pointer (kActivePlayerGuid + kGetObjectByGuid), stable across
    // multiple separate weapon swaps.
    constexpr size_t kUnitFieldArrayOffset = 0x1958;

    // --- type masks ---
    constexpr uint32_t kTypeMaskUnit   = 0x08;
    constexpr uint32_t kTypeMaskPlayer = 0x10;

    // --- signatures ---
    using GetObjectFn        = void*(__cdecl*)(unsigned long long guid, unsigned typemask,
                                               const char* tag, int flag);
    using ActivePlayerGuidFn = unsigned long long(__cdecl*)();
    using ReactionFn         = int(__fastcall*)(void* self, void* edx, void* other);

    // --- typed views over the objects above ---
    // The constants are the curated landmarks; these structs give named, typed access to the same fields,
    // with every member offset checked against a constant at compile time (a wrong padding fails the build).
    // Only RE'd fields are named; the gaps are explicit padding. Pointers are 4 bytes on the 32-bit client.
#pragma pack(push, 1)
    /** @brief Unit / world object: the body-model slot and the world position. */
    struct UnitObject
    {
        uint8_t  _pad00[kUnitModelField];
        void*    model;            // kUnitModelField -> body model
        uint8_t  _pad01[kUnitPositionField - kUnitModelField - sizeof(void*)];
        float    position[3];      // kUnitPositionField -> world position x, y, z
    };
    static_assert(offsetof(UnitObject, model) == kUnitModelField, "UnitObject.model");
    static_assert(offsetof(UnitObject, position) == kUnitPositionField, "UnitObject.position");

    /** @brief Model object: the parent slot in the attachment chain. */
    struct ModelObject
    {
        uint8_t  _pad00[kModelParentField];
        void*    parent;           // kModelParentField -> parent model (0 = root)
    };
    static_assert(offsetof(ModelObject, parent) == kModelParentField, "ModelObject.parent");
#pragma pack(pop)
}
