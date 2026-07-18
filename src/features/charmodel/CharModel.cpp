// Character-model equipment detours: publish item slot change/clear events around the native slot handlers.
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

#include "config.hpp"
#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"
#include "engine/events/Event.hpp"

#include "offsets/game/M2.hpp"

#include <cstdint>

namespace
{
    namespace ev = wxl::events;
    namespace m2 = wxl::offsets::game::m2;

    m2::M2_SlotDispatchFn g_origSlotDispatch = nullptr;
    m2::M2_SlotClearFn    g_origSlotClear    = nullptr;

    /**
     * @brief Detours the CharModel equip-slot handler, emitting OnItemSlotChange then calling the native.
     *
     * Fires when an item is equipped to an internal model slot (not the WoW equipment slot index).
     * modelSlot maps to an equipment category (head, chest, weapon, etc.). itemDataPtr points to
     * the item data block that carries the display_id used to look up ItemDisplayInfo.
     * @param cmo          CharModelObject this pointer.
     * @param edx          thiscall dummy.
     * @param modelSlot    internal model slot index.
     * @param itemDataPtr  item data block pointer (contains display_id).
     * @param postFlag     native post-dispatch flag.
     */
    void __fastcall hkSlotDispatch(void* cmo, void* edx, uint32_t modelSlot, void* itemDataPtr, uint32_t postFlag)
    {
        // Native must run first: for head (slot 11), the native handler checks if slot 11 is occupied and
        // returns NULL if so -- which would skip geoset writes. Let native populate slot 11 first,
        // then subscribers receive the event with the slot already in its post-dispatch state.
        g_origSlotDispatch(cmo, edx, modelSlot, itemDataPtr, postFlag);
        ev::ItemSlotChangeArgs a{ cmo, modelSlot, itemDataPtr };
        ev::Emit(ev::Event::OnItemSlotChange, &a);
    }

    /**
     * @brief Detours the CharModel equip-slot clear, emitting OnItemSlotClear then calling the native.
     *
     * Fires when a WoW equipment slot is cleared on a CharModelObject, detaching any M2 that was
     * loaded for that slot and releasing its render context.
     * @param cmo           CharModelObject this pointer.
     * @param edx           thiscall dummy.
     * @param equipSlotWow  WoW equipment slot index (EQUIPMENT_SLOT_* constants, 0-18).
     */
    void __fastcall hkSlotClear(void* cmo, void* edx, uint32_t equipSlotWow)
    {
        ev::ItemSlotClearArgs a{ cmo, equipSlotWow };
        ev::Emit(ev::Event::OnItemSlotClear, &a);
        g_origSlotClear(cmo, edx, equipSlotWow);
    }

    bool InstallCharModel()
    {
        wxl::hook::Install("CharModelSlotDispatch", m2::kCharModelSlotDispatch,
                           &hkSlotDispatch, &g_origSlotDispatch);
        wxl::hook::Install("CharModelSlotClear", m2::kCharModelSlotClear,
                           &hkSlotClear, &g_origSlotClear);
        return true;
    }
}

WXL_REGISTER_FEATURE("charmodel", wxl::features::kCharModel, InstallCharModel)
