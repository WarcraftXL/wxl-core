// Sound system addresses: the master-volume control and its init guard.
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

// INTERNAL to the core. The client sound-system master-volume control. Modules never include this; they
// use wxl::game::sound.
namespace wxl::offsets::engine::sound
{
    // Master-volume setter (volume in 0..1): writes the value to the sound group and flags it dirty;
    // the mixer applies it on the next audio tick. No-ops while the sound system is not up.
    constexpr uintptr_t kSetMasterVolume = 0x00879460;
    using SetMasterVolumeFn = void(__cdecl*)(float volume);

    // PlaySound script function: plays a 2D UI sound by id/name. __cdecl(scriptState); the sound id/name
    // is on the script stack. A detour observes (or, by skipping the original, suppresses) UI/world sounds.
    constexpr uintptr_t kPlaySound = 0x009858B0;
    using PlaySoundFn = int(__cdecl*)(void* scriptState);

    // Non-zero once the sound system is initialized.
    constexpr uintptr_t kSoundActiveFlag = 0x00D43814;

    // SoundKitID playback entry: resolves the given SoundKitID against the loaded SoundEntriesRec index
    // and, on a hit with a populated file-variant list, plays it. Covers spell/creature/UI
    // SoundKitID-driven playback. Returns 5 when the id is not found, 6 when the record has no file
    // variant for the current locale/gender -- both before any file I/O is attempted.
    constexpr uintptr_t kPlaySoundKit = 0x004C6A40;
    using PlaySoundKitFn = int(__cdecl*)(int soundKitId, int p2, int p3, int* p4, int p5,
                                          uint32_t* p6, uint32_t p7, int p8);

    // Sound-group array pointer; the first group's field at +0x08 holds the live master-volume float.
    constexpr uintptr_t kSoundGroupArrayPtr  = 0x00D438FC;
    constexpr size_t    kOffGroupMasterVolume = 0x08;

    // --- typed view over a sound-group record ---
    // The constants above are the curated landmarks; this struct gives named, typed access to the same
    // field, with the member offset checked against the constant at compile time. Only the field of
    // interest is named; the lead-in is explicit padding.
#pragma pack(push, 1)
    /** @brief Sound-group record (an element of the kSoundGroupArrayPtr target). */
    struct SoundGroup
    {
        uint8_t  _pad00[kOffGroupMasterVolume];
        float    masterVolume;     // kOffGroupMasterVolume (live master volume, 0..1)
    };
    static_assert(offsetof(SoundGroup, masterVolume) == kOffGroupMasterVolume, "SoundGroup.masterVolume");
#pragma pack(pop)
}
