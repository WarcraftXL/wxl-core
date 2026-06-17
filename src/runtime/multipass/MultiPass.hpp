// Shared texture-composition mechanism: layer extra textures over a surface the Client draws once.
// Copyright (C) 2026 WraithEngine
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

// DLL-only (runtime / GPU). The shared texture-composition mechanism.
//
// The Client draws each surface in a single textured pass. Modern assets layer several textures over one
// surface: an additive glow or emissive, a multiplied lightmap or detail map, a second blended diffuse.
// The Client has no single-pass form for that, so this module replays the geometry that is already bound
// once per extra layer, binding the layer texture and the blend mode that combines it with what is already
// in the frame buffer.
//
// Each asset runtime (M2 ribbons, ADT terrain,  WMO map objects) 
// builds a pass list from ITS OWN data and feeds it here; the per-format adapters stay
// with their format (runtime/m2, runtime/adt, runtime/wmo).
namespace wraith::runtime::multipass
{
    // How an extra pass combines with the result already in the frame buffer.
    enum class Blend : uint32_t
    {
        Opaque   = 0, // replace (the base pass the Client already drew)
        Add      = 1, // additive: result + layer (glow / emissive / shine)
        Modulate = 2, // multiply: result * layer (lightmap / detail / ambient occlusion)
    };

    // One extra layer to compose over the base pass. POD; an adapter fills an array of these.
    struct Pass
    {
        void*    texture; // bindable texture handle (from ResolveTexture)
        Blend    blend;   // how this layer combines with the frame buffer
        uint32_t sampler; // texture stage (0 = first)
    };

    // Resolve a bindable texture handle from a loaded texture object. Returns null when it cannot resolve.
    void* ResolveTexture(void* textureObject, bool srgb);

    // Low-level levers shared by the adapters; ordinarily call Emit instead of these directly.
    void BindTexture(uint32_t sampler, void* textureHandle);
    void SetBlend(Blend blend);

    // Re-issues the caller's own geometry (e.g. a batch index range). Called once per extra pass; the
    // module only sets the per-pass texture + blend around it, so the geometry bind stays with the adapter.
    using SubmitFn = void(*)(void* user);

    // Compose `count` extra passes over the currently bound geometry, then restore the opaque blend. The
    // base pass is assumed already drawn by the Client; this only adds the layers on top.
    void Emit(const Pass* passes, uint32_t count, SubmitFn submit, void* user);
}
