// Grass wind + parting: the tunable settings rows, shared with the Lua surface (wxl.wind.*).
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

// The detail-doodad (grass) motion port. 3.3.5 has no ambient wind, so WXL animates the engine's own
// grass on the GPU: a detour on IShaderCreateVertex disassembles the real DetailDoodad vertex shader,
// injects a motion block that displaces each blade in the world plane, and reassembles it (so the bend
// operates on the shader's real inputs — the blade uv.y and the view-space position — no register
// guesswork). The bend weight is 1-uv.y anchored at the base and squared (stiff base, loose tip), so it
// is exact per blade on any terrain. Two travelling sine waves make the wind; an optional repulsion
// parts blades around the player. Constants ride the engine's own per-chunk upload in the free block
// registers c14..c21. Technique recovered from the prior wxl-render-modern grass module; delivered
// through the cleaner IShaderCreateVertex seam.
namespace wxl::features::grasswind
{
    /// Wind row: a directional two-wave sway field over the grass.
    struct WindSettings
    {
        bool  enabled         = true;   // master switch for the sway
        float directionDeg    = 45.0f;  // wind heading in the world XY plane, degrees
        float speed           = 3.0f;   // wave travel speed, yards per second
        float amplitude       = 0.060f; // primary wave sway at the blade tip, yards
        float wavelength      = 18.0f;  // primary wave length, yards
        float crossAmplitude  = 0.02f;  // secondary cross-swell sway, yards
        float crossWavelength = 6.5f;   // secondary wave length, yards
        float crossAngleDeg   = 35.0f;  // secondary wave heading offset from the primary, degrees
        float lean            = 0.35f;  // constant downwind lean, fraction of the primary amplitude
        float variance        = 0.6f;   // per-blade amplitude spread, 0 (uniform) .. 1 (0.5x..1.5x)
        float distanceFade    = 0.015f; // per-yard view-distance sway attenuation, 0 disables
        float anchor          = 0.3f;   // fraction of the blade (from the base) that never moves, 0..0.9
    };

    /// Physics row: blades part around a moving unit (the player).
    struct PhysicsSettings
    {
        bool  enabled     = true;  // master switch for the parting
        float radius      = 2.0f;  // influence radius around the unit, yards
        float forceCenter = 0.5f;  // lean strength at the unit
        float forceEdge   = 0.0f;  // lean strength at the radius edge
        float coneHeight  = 0.5f;  // height above ground where the lean weight reaches 1, yards
        float minHeight   = 0.3f;  // blades shorter than this stay still, yards
    };

    /// The single global settings rows. Valid whether or not the detour installed; the Lua surface reads
    /// and writes them directly (all on the game thread, so no synchronization is needed).
    WindSettings&    Wind();
    PhysicsSettings& Physics();

    /// True when the feature is compiled in AND at least one grass vertex shader was successfully
    /// disassembled, injected and swapped (so tuning actually reaches grass).
    bool Installed();
}
