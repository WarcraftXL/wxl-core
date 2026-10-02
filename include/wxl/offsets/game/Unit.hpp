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
    // Walk every resident object, one callback per object, carrying the GUID as two dwords. Returning 0
    // from the callback stops the walk. Main thread only: the list head is read from thread-local storage,
    // so off it the enumerator walks another thread's manager or none.
    constexpr uintptr_t kEnumObjects = 0x004D4B30;
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
    constexpr size_t kObjectHeaderField = 0x08;  // any object -> header carrying its GUID and type mask
    constexpr size_t kHeaderGuidField   = 0x00;  // header -> GUID
    constexpr size_t kHeaderTypeField   = 0x08;  // header -> type mask; what kGetObjectByGuid filters on
    // unit object -> owned CharacterComponent (the equip/model render state CharEquipSlotUpdate and
    // the geoset/attach pipeline operate on; null for non-humanoid units, or a humanoid unit with no
    // component built yet). Confirmed via disassembly 2026-08-16 at two independent read sites gating
    // CCharacterComponent__AddItemBySlot. Not the same field as kUnitModelField just above -- 0xB4 and
    // 0xB4C are easy to misread as the same offset, they are not.
    constexpr size_t kUnitCharacterComponentField = 0xB4C;

    // --- virtual slots shared by every object type ---
    // Every object type's descendants agree on this part of their vtable. Only these four are common:
    // past them the layouts diverge, and two of the slots below them are stubs on some types.
    constexpr size_t kVtNamePosition = 8;  // anchor above the model, where the client hangs the name
    constexpr size_t kVtPosition     = 11; // world position; the base implementation reports the origin
    constexpr size_t kVtRawPosition  = 12;
    // Orientation, radians counter-clockwise from +X. Confirmed by disassembly 2026-09-11 at two
    // independent call sites (0x004F6A96 and 0x00522112, the latter on the active player straight out
    // of kGetObjectByGuid): both load slot 13, call it with this in ECX and nothing on the stack, and
    // take the result off the FPU stack with fstp, which makes it a float return. Both then feed it
    // straight to fsincos and store cos into x and sin into y, which is what fixes the convention:
    // facing 0 points along +X and the angle increases counter-clockwise.
    constexpr size_t kVtFacing       = 13;

    // --- type masks ---
    // Bit per object category. A lookup passes the set it accepts and yields null for anything else, so
    // these select a category rather than merely describing one.
    constexpr uint32_t kTypeMaskObject        = 0x01;
    constexpr uint32_t kTypeMaskItem          = 0x02;
    constexpr uint32_t kTypeMaskContainer     = 0x04;
    constexpr uint32_t kTypeMaskUnit          = 0x08;
    constexpr uint32_t kTypeMaskPlayer        = 0x10;
    constexpr uint32_t kTypeMaskGameObject    = 0x20;
    constexpr uint32_t kTypeMaskDynamicObject = 0x40;
    constexpr uint32_t kTypeMaskCorpse        = 0x80;

    // --- signatures ---
    using GetObjectFn        = void*(__cdecl*)(unsigned long long guid, unsigned typemask,
                                               const char* tag, int flag);
    using ActivePlayerGuidFn = unsigned long long(__cdecl*)();
    using ReactionFn         = int(__fastcall*)(void* self, void* edx, void* other);
    using EnumStepFn         = int(__cdecl*)(uint32_t guidLow, uint32_t guidHigh, void* user);
    using EnumObjectsFn      = int(__cdecl*)(EnumStepFn step, void* user);
    using PositionFn         = void(__thiscall*)(void* self, float out[3]);
    // No out-param and no stack argument, unlike PositionFn just above: the angle comes back in st(0).
    using FacingFn           = float(__thiscall*)(void* self);

    // --- typed views over the objects above ---
    // The constants are the curated landmarks; these structs give named, typed access to the same fields,
    // with every member offset checked against a constant at compile time (a wrong padding fails the build).
    // Only known fields are named; the gaps are explicit padding. Pointers are 4 bytes on the 32-bit client.
#pragma pack(push, 1)
    /** @brief Unit / world object: the body-model slot, the world position, and the owned character component. */
    struct UnitObject
    {
        uint8_t  _pad00[kUnitModelField];
        void*    model;            // kUnitModelField -> body model
        uint8_t  _pad01[kUnitPositionField - kUnitModelField - sizeof(void*)];
        float    position[3];      // kUnitPositionField -> world position x, y, z
        uint8_t  _pad02[kUnitCharacterComponentField - kUnitPositionField - sizeof(float) * 3];
        void*    characterComponent; // kUnitCharacterComponentField -> owned CharacterComponent
    };
    static_assert(offsetof(UnitObject, model) == kUnitModelField, "UnitObject.model");
    static_assert(offsetof(UnitObject, position) == kUnitPositionField, "UnitObject.position");
    static_assert(offsetof(UnitObject, characterComponent) == kUnitCharacterComponentField,
                  "UnitObject.characterComponent");

    /** @brief Object header: the GUID and the type mask the object lookup filters on. */
    struct ObjectHeader
    {
        unsigned long long guid;     // kHeaderGuidField
        uint32_t           typeMask; // kHeaderTypeField
    };
    static_assert(offsetof(ObjectHeader, guid) == kHeaderGuidField, "ObjectHeader.guid");
    static_assert(offsetof(ObjectHeader, typeMask) == kHeaderTypeField, "ObjectHeader.typeMask");

    /** @brief What every object carries regardless of its concrete type: the header slot. */
    struct ObjectBase
    {
        uint8_t       _pad00[kObjectHeaderField];
        ObjectHeader* header;      // kObjectHeaderField -> GUID + type mask
    };
    static_assert(offsetof(ObjectBase, header) == kObjectHeaderField, "ObjectBase.header");

    /** @brief Model object: the parent slot in the attachment chain. */
    struct ModelObject
    {
        uint8_t  _pad00[kModelParentField];
        void*    parent;           // kModelParentField -> parent model (0 = root)
    };
    static_assert(offsetof(ModelObject, parent) == kModelParentField, "ModelObject.parent");
#pragma pack(pop)
}
