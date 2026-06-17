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

#include "MultiPass.hpp"

#include "Gx.hpp"

#include <cstdint>

namespace gx = wraith::offsets::engine::gx;

// Texture composition over the graphics device. The levers are the device's own: bind a texture to a
// sampler, set the blend mode through the render-state cache, and let the caller re-issue its geometry.
namespace wraith::runtime::multipass
{
    namespace
    {
        // The global holds the graphics-device object pointer.
        void* Device() { return *reinterpret_cast<void**>(gx::kDevicePtr); }

        // Map a composition blend to the device blend mode.
        uint32_t DeviceBlend(Blend blend)
        {
            switch (blend)
            {
                case Blend::Add:      return gx::kBlendAdd;
                case Blend::Modulate: return gx::kBlendModulate;
                case Blend::Opaque:   break;
            }
            return gx::kBlendOpaque;
        }
    }

    void* ResolveTexture(void* textureObject, bool /*srgb*/)
    {
        if (!textureObject)
            return nullptr;
        // The render path resolves with mode 0 and no callback; the texture object already carries its format.
        auto resolve = reinterpret_cast<gx::Gx_ResolveTextureHandleFn>(gx::kResolveTextureHandle);
        return resolve(textureObject, 0, nullptr);
    }

    void BindTexture(uint32_t sampler, void* textureHandle)
    {
        auto setTexture = reinterpret_cast<gx::Gx_SetSamplerTextureFn>(gx::kSetSamplerTexture);
        setTexture(Device(), nullptr, gx::kSamplerStage0 + sampler, textureHandle);
    }

    void SetBlend(Blend blend)
    {
        const uint32_t mode = DeviceBlend(blend);
        auto* cache = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(Device()) + gx::kOffStateCache);
        auto* slot  = reinterpret_cast<uint32_t*>(cache + gx::kOffCacheBlendMode);
        if (*slot != mode)
        {
            // Mark the state dirty first, then store: the next draw re-derives the blend factors.
            reinterpret_cast<gx::Gx_MarkStateDirtyFn>(gx::kMarkStateDirty)(gx::kStateBlendIndex);
            *slot = mode;
        }
    }

    void Emit(const Pass* passes, uint32_t count, SubmitFn submit, void* user)
    {
        if (!passes || !submit)
            return;
        for (uint32_t i = 0; i < count; ++i)
        {
            BindTexture(passes[i].sampler, passes[i].texture);
            SetBlend(passes[i].blend);
            submit(user);
        }
        SetBlend(Blend::Opaque); // leave the device as the base pass found it
    }
}
