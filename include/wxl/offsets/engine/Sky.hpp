// The sky's cloud sheet: its generator, its upload path, and the object it lives in.
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

// INTERNAL to the core. The day-night cloud sheet is not a file: the engine fills a CPU buffer
// with layered noise a few rows per frame and uploads the touched band to one of a texture pair.
// Everything here is what a detour on that generator needs to redraw the same band into the same
// buffer and push it through the same upload.
namespace wxl::offsets::engine::sky
{
    /// The incremental cloud-pattern generator. __fastcall, ecx = the clouds object. Called every
    /// frame from the day-night update; regenerates [rowCursor, rowCursor + rowsPerFrame) of the
    /// pattern buffer and uploads that band.
    constexpr uintptr_t kCloudsGenerate = 0x007EFD00;

    /// __cdecl() -> the day-night info block. Valid whenever a world is up.
    constexpr uintptr_t kDayNightGetInfo = 0x007ECEF0;
    using DayNightGetInfoFn = void*(__cdecl*)();
    /// vec3 in that block: direction of the active celestial light, world space -- the same vector
    /// the engine hands its own model lighting every frame.
    constexpr uintptr_t kInfoLightDir = 0x19C;
    /// float in that block: how far the day has run, 0 at its start through 1 at its end. The
    /// engine keys its own colour tables in half-minutes and reaches them by scaling this value
    /// by kDayHalfMinutes -- which is the unit the retail per-time light rows are keyed in too,
    /// so the same product answers both.
    constexpr uintptr_t kInfoDayFraction = 0x04;
    constexpr float     kDayHalfMinutes  = 2880.0f;

    // --- day-night fog override ---
    /// The engine's own mechanism for replacing the zone fog wholesale (its screen effects use
    /// it): saves the live fog, installs (end * startFraction .. end, colour), and keeps it in
    /// force through every per-frame fog update until cleared. The installed end is still capped
    /// by the engine's own distance ceiling, which is exactly the right behaviour.
    constexpr uintptr_t kSetOverrideFog = 0x007ED870;
    using SetOverrideFogFn = void(__cdecl*)(float startFraction, float endDist,
                                            uint32_t colorArgb, uint32_t skyGate);
    /// Releases the override and restores the fog it saved.
    constexpr uintptr_t kClearOverrideFog = 0x007ED820;
    using ClearOverrideFogFn = void(__cdecl*)();
    /// The live fog colour (u32) the day-night update computed for this frame.
    constexpr uintptr_t kFogColor = 0x00D38BF4;
    /// Sky-draw gate (u32): the override WRITES this from its skyGate argument, and the whole
    /// sky -- dome, clouds, celestials, skybox -- draws only while it is nonzero. The engine's
    /// own screen effect passes zero because a dead world wants no sky; an override that only
    /// wants the fog out of the way must pass the gate's current value back through.
    constexpr uintptr_t kSkyDrawGate = 0x00D38CCC;

    /// __cdecl(void* htexture, int, int) -> CGxTex*, called as (handle, 1, 0).
    constexpr uintptr_t kTextureGetGxTex = 0x004B6CB0;

    /// __cdecl(void* gxTex, int x0, int yStart, int width, int yEnd, int immediate).
    /// The generator's own upload call for the band it just drew.
    constexpr uintptr_t kGxTexUpdate = 0x00681F20;

    // The clouds object, fields read/written by the generator. Offsets from the object base.
    constexpr uintptr_t kCloudsEnabled     = 0x2C; ///< u32, zero disables the whole update
    constexpr uintptr_t kCloudsCoverage    = 0x08; ///< u8, density threshold subtracted from noise
    constexpr uintptr_t kCloudsFullRedraw  = 0x0A; ///< u8, set to redo the whole sheet this frame
    constexpr uintptr_t kCloudsParity      = 0x0B; ///< u8, selects which of the texture pair is drawn to
    constexpr uintptr_t kCloudsScrollScale = 0x0C; ///< f32, phase -> integer scroll conversion
    constexpr uintptr_t kCloudsRowsPerFrame= 0x10; ///< u32, band height regenerated per call
    constexpr uintptr_t kCloudsRowCursor   = 0x14; ///< u32, first row of this call's band
    constexpr uintptr_t kCloudsWidth       = 0x1C; ///< u32, texels per row (the sheet is square)
    constexpr uintptr_t kCloudsStrideShift = 0x20; ///< u32, log2 of the buffer row stride in texels
    constexpr uintptr_t kCloudsLayerCount  = 0x28; ///< u32, noise layers summed by the stock pattern
    constexpr uintptr_t kCloudsRgba        = 0x38; ///< u8*[w<<shift * 4], the uploaded pixels
    constexpr uintptr_t kCloudsDensityByte = 0x44; ///< u8*[w<<shift], feeds the bump map and collision
    constexpr uintptr_t kCloudsScroll      = 0x88; ///< u16, integer scroll derived from the phase
    constexpr uintptr_t kCloudsPhase       = 0x8C; ///< f32, accumulated animation phase
    constexpr uintptr_t kCloudsTexturePair = 0x90; ///< HTEXTURE[2], picked by (parity - 1) & 1

    // Day-night, fog and outdoor light
    /// The whole day-night advance in one call - the coarse-grained hook for a custom time-of-day or
    /// lighting model. __cdecl, caller-cleaned.
    constexpr uintptr_t kUpdate                            = 0x007816F0;
    /// The fog density/blend rate helper shared by the override setters - hooking it changes fog
    /// falloff without touching color. __cdecl, caller-cleaned.
    constexpr uintptr_t kFogRateCompute                    = 0x007ECD00;
    /// The exact producer of the fog color/near/far the terrain and sky then consume - the correct
    /// override point for a retail-style fog model, and it sits above the two already-known override
    /// setters. __cdecl, caller-cleaned.
    constexpr uintptr_t kFogUpdate                         = 0x007F16F0;
    /// Per-map sky/light table construction - the place to add sky records before anything queries
    /// them. __cdecl, caller-cleaned.
    constexpr uintptr_t kInitialize                        = 0x007F2790;
    /// The lookup that decides whether a custom sky replaces the default - an extension can answer it
    /// from its own data and get modern sky records honoured everywhere at once. __cdecl, caller-
    /// cleaned.
    constexpr uintptr_t kSkyOverrideResolve                = 0x007F30C0;
    /// Where every day-night band color is interpolated and published - the single place to inject a
    /// modern LightData color set. __cdecl, caller-cleaned.
    constexpr uintptr_t kColorsResolve                     = 0x007F3230;
    /// The outdoor light resolve (sun direction, ambient, planets) - one detour replaces the entire
    /// outdoor lighting input. __cdecl, caller-cleaned.
    constexpr uintptr_t kLightingUpdate                    = 0x007F3920;

    // Map lifecycle: load / unload / enter / leave
    /// Lets a sky/lighting extension release its per-map sky overrides exactly when the engine does.
    /// __cdecl, caller-cleaned.
    constexpr uintptr_t kMapUnload                         = 0x007F1D30;
}
