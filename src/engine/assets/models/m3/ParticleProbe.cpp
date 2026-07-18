// m3: temporary particle diagnostic, remove after particle bring-up.
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
#include "common/Log.hpp"
#include "engine/events/Event.hpp"
#include "engine/events/EventScript.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace wxl::modern::assets::m3
{
    namespace ev = wxl::events;

    namespace
    {
        // model-instance fields read by the dump
        constexpr size_t kModelShared       = 0x2C;
        constexpr size_t kModelAlpha        = 0x198;
        constexpr size_t kModelSampledBase  = 0x2C0; // per-emitter sampled cells, stride 0x88
        constexpr size_t kModelEmitterArray = 0x2C4; // per-emitter object pointers
        constexpr size_t kSharedPathStem    = 0x3C;
        constexpr size_t kSharedM2Data      = 0x150;
        constexpr size_t kHdrEmitterCount   = 0x128;
        constexpr size_t kSampledStride     = 0x88;

        template <typename T>
        T Rd(const void* base, size_t off)
        {
            T v{};
            std::memcpy(&v, static_cast<const uint8_t*>(base) + off, sizeof(T));
            return v;
        }
    }

    /**
     * @brief Once-per-second dump of the live emitter state of the probed model.
     *
     * Opt-in via WXL_PARTICLE_PROBE: unarmed, its per-frame handler is never subscribed, so the
     * per-visible-M2 name scan costs nothing in normal play.
     */
    class ParticleProbe final : public events::EventScript
    {
    public:
        ParticleProbe()
        {
            // Subscribe the probe only when the modern-M3 format is compiled in; disabled, the instance is inert.
            if constexpr (!wxl::features::kModernM3) return;
            char env[8]{};
            const DWORD n = GetEnvironmentVariableA("WXL_PARTICLE_PROBE", env, sizeof env);
            const bool armed = n > 0 && env[0] != '0' && env[0] != 'n' && env[0] != 'N' &&
                               env[0] != 'f' && env[0] != 'F';
            if (!armed) return;
            on<&ParticleProbe::OnPerFrame>(ev::Event::OnM2PerFrameUpdate);
            WLOG_INFO("m3-probe: armed (per-frame emitter state, felstalker)");
        }

    private:
        void OnPerFrame(const ev::M2PerFrameUpdateArgs& a)
        {
            const uint8_t* model = static_cast<const uint8_t*>(a.renderCtx);
            if (!model) return;
            const uint8_t* shared = Rd<const uint8_t*>(model, kModelShared);
            if (!shared) return;

            const char* stem = reinterpret_cast<const char*>(shared + kSharedPathStem);
            bool match = false;
            for (const char* p = stem; *p && p < stem + 0x100; ++p)
            {
                if ((*p == 'f' || *p == 'F') && _strnicmp(p, "felstalker", 10) == 0)
                {
                    match = true;
                    break;
                }
            }
            if (!match) return;

            const uint8_t* m2data = Rd<const uint8_t*>(shared, kSharedM2Data);
            if (!m2data) return;
            const uint32_t nEmit = Rd<uint32_t>(m2data, kHdrEmitterCount);
            if (!nEmit || nEmit > 8) return;

            const DWORD now = GetTickCount();
            if (now - lastLog_ < 1000) return;
            lastLog_ = now;

            const uint8_t* sampledBase = Rd<const uint8_t*>(model, kModelSampledBase);
            const uint8_t* const* emitters =
                Rd<const uint8_t* const*>(model, kModelEmitterArray);
            const float alpha = Rd<float>(model, kModelAlpha);
            WLOG_INFO("m3-probe: model=%p flags=0x%X parent=%p scene=%p alpha=%.3f emitters=%u",
                      model, Rd<uint32_t>(model, 0x10), Rd<const void*>(model, 0x48),
                      Rd<const void*>(model, 0x28), alpha, nEmit);
            // texture handle table: per texture entry, the async-pending and gx objects
            const uint8_t* const* texTable = Rd<const uint8_t* const*>(shared, 0x174);
            const uint32_t nTex = Rd<uint32_t>(m2data, 0x50);
            for (uint32_t t = 0; texTable && t < nTex && t < 8; ++t)
            {
                const uint8_t* th = texTable[t];
                if (th)
                    WLOG_INFO("m3-probe:  tex[%u] h=%p flags=0x%X async=%p gx=%p",
                              t, th, Rd<uint32_t>(th, 0x28), Rd<const void*>(th, 0x40),
                              Rd<const void*>(th, 0x44));
                else
                    WLOG_INFO("m3-probe:  tex[%u] h=null", t);
            }
            for (uint32_t i = 0; i < nEmit; ++i)
            {
                const uint8_t* cell = sampledBase ? sampledBase + i * kSampledStride : nullptr;
                const uint8_t* em = emitters ? emitters[i] : nullptr;
                if (cell)
                    WLOG_INFO("m3-probe:  [%u] cell enable=%u on=%u gate=%u rate=%.2f life=%.2f",
                              i, Rd<uint8_t>(cell, 0x80), Rd<uint8_t>(cell, 0x84),
                              Rd<uint8_t>(cell, 0x85), Rd<float>(cell, 0x50),
                              Rd<float>(cell, 0x44));
                if (em)
                {
                    WLOG_INFO("m3-probe:  [%u] em=%p flags=0x%X flags2bit0=%u rate=%.2f count=%u "
                              "drawn=%u texh=%p",
                              i, em, Rd<uint32_t>(em, 0x134), Rd<uint32_t>(em, 0x138) & 1,
                              Rd<float>(em, 0x9C), Rd<uint32_t>(em, 0x50),
                              Rd<uint32_t>(em, 0x1C), Rd<const void*>(em, 0x128));
                    // first live particle, full record (0x20-stride pool + index list)
                    const uint32_t cnt = Rd<uint32_t>(em, 0x50);
                    const uint8_t* pool = Rd<const uint8_t*>(em, 0x34);
                    const uint32_t* idx = Rd<const uint32_t*>(em, 0x54);
                    if (cnt && pool && idx)
                    {
                        const uint8_t* p = pool + idx[0] * 0x20;
                        float f[8];
                        std::memcpy(f, p, sizeof(f));
                        WLOG_INFO("m3-probe:      p0[%u] %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f",
                                  idx[0], f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]);
                    }
                }
            }
        }

        DWORD lastLog_ = 0;
    };

    // File-scope instance self-registers its handler at DLL load.
    ParticleProbe g_particleProbe;
}
