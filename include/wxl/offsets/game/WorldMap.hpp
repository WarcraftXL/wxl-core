// World-map screen entries: zone sync and the world enter / leave edges.
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

// INTERNAL to the core. The world-map screen: the zone-to-map binding and the two edges it is brought
// up and torn down on. These are the entries an extension reaches by name through the core's hook-point
// table (see runtime/HookPoints.cpp); each line states what a detour there controls and the calling
// convention. No signature typedef is declared: the conventions are confirmed, the parameter types are
// not, and a wrong typedef is worse than none. Modules never include this; they use wxl::game.
namespace wxl::offsets::game::worldmap
{
    // --- area / zone resolution ---
    /// World-position to map-area projection used by the map UI, so an extension can add overlays that
    /// agree with the engine's own hit regions. __cdecl, caller-cleaned.
    constexpr uintptr_t kAreaFromPosition = 0x00543E50;
    /// Runs on every zone transition with the resolved zone state: the place to raise a "player changed
    /// zone" event, and its nine call sites make it hard to miss one. __cdecl, caller-cleaned.
    constexpr uintptr_t kZoneSync         = 0x00547170;

    // --- world enter / leave edges ---
    /// Runs when the player is actually in the world, not merely when a map file loads, which is what
    /// makes it the right trigger for a "world entered" event. __cdecl, caller-cleaned.
    constexpr uintptr_t kEnterWorld       = 0x00547120;
    /// The matching "world left" edge, ahead of the loading screen for the next destination. __cdecl,
    /// caller-cleaned.
    constexpr uintptr_t kLeaveWorld       = 0x00547150;
}
