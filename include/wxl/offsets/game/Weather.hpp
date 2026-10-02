// The live weather state: storm intensity and precipitation kind, as the world keeps them.
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

// INTERNAL to the core. Modules never include this; they use wxl::water.
namespace wxl::offsets::game::weather
{
    // The live weather object -- a POINTER, not the object itself. Loaded once per frame by the scene
    // render before it dispatches into the weather renderer, which settles the object's identity.
    constexpr uintptr_t kWorldWeather = 0x00CD7544;

    // Current storm intensity. The engine's intensity setter does not snap this: it walks the value
    // from where it was toward its target over a duration derived from the size of the change, so
    // what is stored here is already a smooth ramp and can be read raw every frame.
    constexpr size_t kIntensity = 0x008;
    // The same function's own dead zone: below this nothing downstream reacts, and 0.25..1 is
    // remapped onto 0..1. Reused rather than replaced so a sea rises exactly when the sky does.
    constexpr float  kIntensityKnee = 0.25f;

    // Non-zero counts of live precipitation of each kind; the engine's type query reports 1/2/3 by
    // testing them in this order, and 0 (fine) when all three are clear.
    constexpr size_t kRainCount = 0x13C;
    constexpr size_t kSnowCount = 0x140;
    constexpr size_t kSandCount = 0x144;

    // Weather
    /// Direct control of storm strength, the parameter that scales precipitation counts and sound - the
    /// single knob an extension needs for weather intensity. __thiscall, 2 stack args.
    constexpr uintptr_t kStormIntensitySet                 = 0x00784850;
    /// A small, cheap gate around all weather drawing - ideal for suppressing or replacing
    /// precipitation rendering without touching the simulation. __thiscall, caller-cleaned.
    constexpr uintptr_t kRender                            = 0x0078CA50;
    /// The reset that runs on weather change and on map unload - the notification an extension needs to
    /// drop its own precipitation state. __thiscall, caller-cleaned.
    constexpr uintptr_t kClear                             = 0x0078D0B0;
    /// The weather simulation step - lets an extension drive precipitation/particle state directly
    /// instead of racing the server-driven state. __thiscall, caller-cleaned.
    constexpr uintptr_t kUpdate                            = 0x0078D170;
}
