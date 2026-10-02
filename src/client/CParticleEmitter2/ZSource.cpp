// Reads the source emitter's "no point source" sentinel the way the source content means it.
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

// An emitter's z source is a point the particle is pushed away from, and "there is no such point" is
// spelled two different ways: source content writes a far-away sentinel, the target pipeline expects
// zero. Read literally, the sentinel turns a rising emission cone into a sideways spray, because the
// point-source branch replaces the cone with a direction away from that point.
//
// This is a per-emitter runtime value, not a record field: the same emitter is re-armed every time it
// is rebuilt, so translating the sentinel where the value is consumed covers both the build and the
// per-frame path, and leaves the model bytes exactly as the file has them.

#include "common/Log.hpp"
#include "engine/hook/Hook.hpp"
#include "runtime/HookPoints.hpp"
#include "engine/hook/Registry.hpp"
#include "wxl/offsets/game/M2.hpp"

namespace
{
    namespace off = wxl::offsets::game::m2;

    constexpr float kSourceNoPointSource = 255.0f;
    constexpr float kTargetNoPointSource = 0.0f;

    off::M2_SetZsourceFn g_origSetZsource = nullptr;

    void __fastcall hkSetZsource(void* emitter, void* edx, float zSource)
    {
        if (zSource == kSourceNoPointSource) zSource = kTargetNoPointSource;
        g_origSetZsource(emitter, edx, zSource);
    }

    bool InstallParticleZSource()
    {
        if (!wxl::runtime::hookpoints::Attach("M2.ParticleSetZSource", &hkSetZsource, &g_origSetZsource))
            return false;
        WLOG_INFO("m2native-particles: emitter z-source sentinel translated at its read");
        return true;
    }
}

WXL_REGISTER_FEATURE("m2native-particles", true, InstallParticleZSource)
