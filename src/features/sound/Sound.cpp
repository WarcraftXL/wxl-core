// Sound detours: publish the play-sound event and log unresolved SoundKit ids for diagnosis.
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

#include "common/Log.hpp"
#include "offsets/engine/Sound.hpp"

#include <cstdint>
#include <unordered_set>

namespace
{
    namespace ev  = wxl::events;
    namespace snd = wxl::offsets::engine::sound;

    snd::PlaySoundFn    g_origPlaySound    = nullptr;
    snd::PlaySoundKitFn g_origPlaySoundKit = nullptr;

    /**
     * @brief Detours the play-sound API, emitting OnSoundPlay before the sound starts.
     *
     * Fires on every UI/world sound. Logs the first fire only.
     * @param scriptState  script state the call ran on (the sound id/name is on its stack).
     * @return the native function result.
     */
    int __cdecl hkPlaySound(void* scriptState)
    {
        ev::SoundPlayArgs a{ scriptState };
        ev::Emit(ev::Event::OnSoundPlay, &a);

        static bool logged = false;
        if (!logged) { logged = true; WLOG_INFO("sound: play hook active"); }
        return g_origPlaySound(scriptState);
    }

    /**
     * @brief Diagnostic-only: logs every SoundKitID play attempt and its result code.
     *
     * Disambiguates a data resolution gap (returns 5/6, no file I/O ever attempted) from a
     * cache-served hit (returns success with no corresponding Storage_FileOpen) for the in-world
     * interface/spell/creature sound investigation.
     */
    int __cdecl hkPlaySoundKit(int soundKitId, int p2, int p3, int* p4, int p5,
                               uint32_t* p6, uint32_t p7, int p8)
    {
        int r = g_origPlaySoundKit(soundKitId, p2, p3, p4, p5, p6, p7, p8);
        // Each failing kit id is logged once; the raw stream repeats the same missing ids dozens of
        // times per minute and the per-line file write is itself a frame cost.
        if (r != 0)
        {
            static std::unordered_set<int> loggedKits;
            if (loggedKits.insert(soundKitId).second)
                WLOG_INFO("audio-diag: PlaySoundKit id=%d -> %d", soundKitId, r);
        }
        return r;
    }

    bool InstallSound()
    {
        wxl::hook::Install("PlaySound", snd::kPlaySound, &hkPlaySound, &g_origPlaySound);
        wxl::hook::Install("PlaySoundKit", snd::kPlaySoundKit, &hkPlaySoundKit, &g_origPlaySoundKit);
        return true;
    }
}

WXL_REGISTER_FEATURE("sound", wxl::features::kSound, InstallSound)
