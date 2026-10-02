// Character-creation screen state: the per-race preference array and every instruction that carries
// its base address or its capacity as an immediate.
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

// INTERNAL to the core. Character-creation globals and the code sites that hardcode their extent.
namespace wxl::offsets::game::charcreate
{
    // --- CCharacterCreation::m_charPreferences ------------------------------------------------
    // A flat array of void*, one slot per (race, sex) pair, indexed `sex + raceId * 2` where raceId
    // is a real ChrRaces id (SetSelectedRace reads it out of the available-race list at 0x00B6B1D4,
    // never the list position). There is NO bounds check on that index at any of the three sites
    // that use it, and the array is sized for ids 0..21 only.
    //
    // Each live slot holds a 0x20-byte SMemAlloc block carrying that race/sex combination's last
    // chosen customization. Initialize memsets the whole array; FreeCharacterCreationData frees and
    // re-nulls every slot, walking kPreferenceRaces outer iterations of kPreferenceSexes dwords.
    //
    // What an id >= kPreferenceRaces writes a live heap pointer over, in address order past the end:
    // m_raceIndex (kRaceIndex), the customize frame, the live CCharacterComponent* (0x00B6B1A0),
    // the class list, and the available-race list's own count and data pointer (0x00B6B1D0 /
    // 0x00B6B1D4). Id 33 lands on 0x00B6B1D8 / 0x00B6B1DC, the list's chunk size and the head of
    // the array after it.
    constexpr uintptr_t kPreferences      = 0x00B6B0D0;
    constexpr uint32_t  kPreferenceSexes  = 2;
    constexpr uint32_t  kPreferenceRaces  = 0x16;  // 22 slots of races, ids 0..21
    constexpr uint32_t  kPreferenceBytes  = 0xB0;  // kPreferenceRaces * kPreferenceSexes * sizeof(void*)

    // First global past the array's end, and therefore the first thing an out-of-range write lands
    // on: the SELECTED POSITION in the available-race list, not a ChrRaces id.
    constexpr uintptr_t kRaceIndex = 0x00B6B180;

    // Every occurrence of kPreferences as a 4-byte operand in the image -- eight, verified by
    // scanning the whole file for the literal, so the set is closed. Each address below is the
    // disp32/imm32 field of one complete instruction, so writing a new base over it changes only
    // which array the instruction reaches. Relocating the array means rewriting ALL of them:
    // a subset leaves some sites indexing the new block and some the old one.
    inline constexpr uintptr_t kPreferenceBaseSites[] = {
        0x004E157D, // SetSelectedSex:              mov eax,[eax*4+base]
        0x004E15B5, // SetSelectedSex:              mov [edx*4+base],eax
        0x004E16A3, // SetSelectedSex:              mov eax,[ecx*4+base]
        0x004E1C3A, // Initialize:                  push base (memset destination)
        0x004E1E94, // FreeCharacterCreationData:   mov esi,base (the walk cursor)
        0x004E20EE, // SetSelectedRace:             mov eax,[edx*4+base]
        0x004E2127, // SetSelectedRace:             mov [edx*4+base],eax
        0x004E222A, // SetSelectedRace:             mov ecx,[ecx*4+base]
    };

    // The two sites that carry the array's CAPACITY rather than its base.
    //
    // Initialize's `push 0xB0` -- the LENGTH argument of its memset. The sequence is
    // `push len / push 0 / push base / call memset` (cdecl), so this immediate is a byte count and
    // must be rewritten in bytes, not slots.
    constexpr uintptr_t kPreferenceInitLengthSite = 0x004E1C34;
    // FreeCharacterCreationData's `mov dword [ebp-4], 0x16` -- the OUTER loop count, in races. The
    // `mov edi, 2` at 0x004E1EA0 is the inner sex count and the body does `add esi, 4` per slot, so
    // the loop frees exactly count * kPreferenceSexes dwords. Both loops are do/while: a count of 0
    // would walk 2^32 slots.
    constexpr uintptr_t kPreferenceFreeRaceCountSite = 0x004E1E9B;
}
