// Sound game bindings: read and set the client's master volume.
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

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/engine/Sound.hpp"

/**
 * @brief Read and set the client's master volume.
 *
 * A module mutes the game with SetMasterVolume(0) and restores it later with the value MasterVolume()
 * returned before muting.
 */
namespace wxl::game::sound
{
    namespace off = wxl::offsets::engine::sound;

    /** @brief Returns true once the client sound system is initialized. */
    inline bool Available()
    {
        return *reinterpret_cast<int*>(off::kSoundActiveFlag) != 0;
    }

    /**
     * @brief Sets the client master volume.
     * @param volume  the new master volume, 0..1. Ignored by the client while the sound system is not up.
     */
    inline void SetMasterVolume(float volume)
    {
        Native<off::SetMasterVolumeFn>(off::kSetMasterVolume)(volume);
    }

    /**
     * @brief Reads the current client master volume.
     * @return the master volume in 0..1, or 1.0 if the sound system is not up.
     */
    inline float MasterVolume()
    {
        if (!Available()) return 1.0f;
        // kSoundGroupArrayPtr is a fixed-address global slot; the deref reads the first group record.
        void* group0 = *reinterpret_cast<void**>(off::kSoundGroupArrayPtr);
        if (!group0) return 1.0f;
        return static_cast<off::SoundGroup*>(group0)->masterVolume;
    }
}
