// Emitter budget: keeps the two crowd-thinning rules from emptying an emitter that has no crowd.
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

// Two separate rules budget an emitter, and both start from the same quantity: how many particles it
// expects to have alive at once, its emission rate times its particle lifetime. Both were written for
// DENSE emitters, where rounding a size or throttling a rate only thins a crowd. Source content also
// authors single-billboard effects -- one particle at a time, the whole look carried by that
// particle's flipbook and its fade -- and for those, the same two rules empty the effect outright:
//
//   Pool size     gets a 15% margin so a particle's replacement can be allocated while the particle it
//                 replaces is still fading, then is rounded to NEAREST -- which rounds the margin away
//                 for any expected count under about 1.3 and lands on a single slot. With one slot the
//                 replacement cannot be allocated until the live particle is released, so the emission
//                 period stops following the rate and becomes the LIFETIME instead, leaving a frame
//                 with nothing alive every cycle.
//   Rate falloff  scales emission down to a quarter as the camera pulls away, unless an opt-out flag
//                 is set. That flag is a PLACEMENT property -- nothing a model contains can ask for
//                 it. A quarter of a crowd is still a crowd; a quarter of one particle is a gap.
//
// Both are corrected in the sizing pass, which runs once per frame per enabled emitter, before the
// emission pass, and is shared by every emitter kind. Neither correction ever tightens a limit: the
// sizing call refuses to shrink a pool, and the opt-out only ever removes a throttle. What they cost
// is bounded by what they protect -- an emitter this touches is one holding a handful of particles.

#include "common/Log.hpp"
#include "engine/hook/Hook.hpp"
#include "runtime/HookPoints.hpp"
#include "engine/hook/Registry.hpp"
#include "wxl/offsets/game/M2.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
    namespace off = wxl::offsets::game::m2;

    /// A relay needs two slots: one fading out while its replacement fades in.
    constexpr uint32_t kMinPool = 2;
    /// Below one expected particle there is nothing left to thin.
    constexpr float kMinLive = 1.0f;

    off::M2_EmitterSyncFn g_origSync = nullptr;

    float Field(const void* emitter, size_t at)
    {
        float v;
        std::memcpy(&v, static_cast<const uint8_t*>(emitter) + at, sizeof v);
        return v;
    }

    /// Particles the emitter expects to have alive at once -- what both rules are really budgeting for.
    float ExpectedLive(const void* emitter)
    {
        return (Field(emitter, off::kOffEmitterRate) + Field(emitter, off::kOffEmitterRateVary)) *
               (Field(emitter, off::kOffEmitterLifespan) + Field(emitter, off::kOffEmitterLifespanVary));
    }

    /// The pool size the sizing rule asks for, rounded up instead of to nearest.
    uint32_t WantedPool(const void* emitter)
    {
        const float wanted = ExpectedLive(emitter) * off::kEmitterPoolHeadroom;
        // The comparison form rejects a NaN too, which a track still being filled can produce.
        if (!(wanted > 0.0f)) return kMinPool;
        if (wanted >= static_cast<float>(off::kEmitterPoolCeiling)) return off::kEmitterPoolCeiling;
        const auto slots = static_cast<uint32_t>(std::ceil(wanted));
        return slots < kMinPool ? kMinPool : slots;
    }

    /// Raises one emitter's pool to pool, and lifts the rate falloff off it when the deepest cut the
    /// falloff can make would leave it with under one particle.
    void ApplyBudget(void* emitter, uint32_t pool)
    {
        reinterpret_cast<off::M2_EmitterSyncAllocationFn>(off::kEmitterSyncAllocation)(
            emitter, nullptr, pool);

        if (ExpectedLive(emitter) * off::kEmitterRateFloor >= kMinLive) return;
        auto* flags = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(emitter) + off::kOffEmitterFlags);
        *flags |= off::kEmitterIgnoreDistance;
    }

    /**
     * @brief Re-budgets the emitter and its children once it has budgeted them its own way.
     *
     * Runs after the original so every request can only raise what it decided, and the sizing call
     * early-outs whenever the pool already covers the request -- a dense emitter pays one compare.
     */
    void __fastcall hkEmitterSync(void* emitter, void* edx)
    {
        g_origSync(emitter, edx);

        const uint32_t pool = WantedPool(emitter);
        ApplyBudget(emitter, pool);

        // A child emitter's pool is its own size multiplied by its parent's, under a shared ceiling.
        auto* bytes = static_cast<uint8_t*>(emitter);
        uint32_t children = 0;
        std::memcpy(&children, bytes + off::kOffEmitterChildCount, sizeof children);
        for (uint32_t i = 0; i < children; ++i)
        {
            void* child = nullptr;
            std::memcpy(&child, bytes + off::kOffEmitterChildArray + i * sizeof(void*), sizeof child);
            if (!child) continue;
            const uint64_t want = static_cast<uint64_t>(WantedPool(child)) * pool;
            ApplyBudget(child, want > off::kEmitterPoolCeiling ? off::kEmitterPoolCeiling
                                                              : static_cast<uint32_t>(want));
        }
    }

    bool InstallEmitterBudget()
    {
        if (!wxl::runtime::hookpoints::Attach("M2.EmitterSync", &hkEmitterSync, &g_origSync))
            return false;
        WLOG_INFO("m2native-particles: emitter budget keeps its margin and its range");
        return true;
    }
}

WXL_REGISTER_FEATURE("particle-emitter-budget", true, InstallEmitterBudget)
