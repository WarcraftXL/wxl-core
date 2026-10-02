// Blend-state factor tables: relocated into our own storage so states can be added to them.
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

// A blend state is a pair of factors -- how much of the incoming pixel, how much of what is already
// there. The device resolves a state index into that pair through two parallel tables, and those
// tables are fixed-size arrays sitting back to back in read-only data: the last state of the first one
// is immediately followed by the first state of the second, so there is nowhere to append.
//
// Source content asks for blend states this device's tables stop short of. Rather than remap those
// requests onto a state that merely looks similar -- which is what discards the difference -- the
// tables are COPIED into storage of our own with room to spare, the missing states are appended, and
// the device is pointed at the copies. From then on the device genuinely has the states, and every
// consumer that resolves an index gets the right factors without knowing anything changed.
//
// Repointing is two writes. Each table has exactly one reader, and it reads with a single instruction
// carrying the table address as an absolute displacement, so rewriting those displacements moves the
// tables without moving or rewriting a single instruction's behaviour.

#include "common/Log.hpp"
#include "common/Mem.hpp"
#include "engine/hook/Registry.hpp"
#include "wxl/offsets/engine/Gx.hpp"

#include <cstdint>
#include <cstring>

namespace
{
    namespace gx = wxl::offsets::engine::gx;

    /// Room for the states the target ships plus the ones later versions of the format ask for.
    constexpr uint32_t kOwnedStateCount = 16;

    /// One appended state: the index it takes, and the factor pair it resolves to.
    struct AddedState
    {
        uint32_t index;
        uint32_t srcFactor;
        uint32_t dstFactor;
        const char* what;
    };

    // Recovered from the donor images, whose tables carry the target's states unchanged and then
    // continue. Only the states something in this engine can actually ask for are added.
    constexpr AddedState kAddedStates[] = {
        { gx::kBlendStateScreenAdd, gx::kD3dBlendInvDestColor, gx::kD3dBlendOne,
          "screen add (saturates toward white instead of clipping)" },
        { gx::kBlendStatePremulAlpha, gx::kD3dBlendOne, gx::kD3dBlendInvSrcAlpha,
          "premultiplied alpha (translucent and additive from one texel)" },
    };

    // Storage the device reads for the rest of the process. Static, so it outlives every device reset.
    uint32_t g_srcFactors[kOwnedStateCount]{};
    uint32_t g_dstFactors[kOwnedStateCount]{};

    /// Rewrites the absolute table address inside one read instruction.
    bool Repoint(uintptr_t displacementField, const uint32_t* table)
    {
        auto* dst = reinterpret_cast<void*>(displacementField);
        const auto value = reinterpret_cast<uint32_t>(table);
        return wxl::mem::WithWritable(dst, sizeof value,
                                      [&] { std::memcpy(dst, &value, sizeof value); });
    }

    bool InstallBlendStates()
    {
        static_assert(gx::kBlendStateCount < kOwnedStateCount, "no room for added states");
        static_assert(gx::kBlendStatePremulAlpha < kOwnedStateCount, "added state past our storage");

        std::memcpy(g_srcFactors, reinterpret_cast<const void*>(gx::kBlendSrcFactors),
                    gx::kBlendStateCount * sizeof(uint32_t));
        std::memcpy(g_dstFactors, reinterpret_cast<const void*>(gx::kBlendDstFactors),
                    gx::kBlendStateCount * sizeof(uint32_t));

        for (const AddedState& state : kAddedStates)
        {
            g_srcFactors[state.index] = state.srcFactor;
            g_dstFactors[state.index] = state.dstFactor;
        }

        // Source first: until both are repointed the device would pair a new source factor with a
        // stale destination one, and the source table is the one read first within a state change.
        if (!Repoint(gx::kBlendSrcReadDisp, g_srcFactors)) return false;
        if (!Repoint(gx::kBlendDstReadDisp, g_dstFactors))
        {
            Repoint(gx::kBlendSrcReadDisp, reinterpret_cast<const uint32_t*>(gx::kBlendSrcFactors));
            return false;
        }

        for (const AddedState& state : kAddedStates)
            WLOG_INFO("gx-blend: state %u added -- %s", state.index, state.what);
        return true;
    }
}

WXL_REGISTER_FEATURE_PHASED("gx-blend-states", true, InstallBlendStates, ::wxl::hook::Phase::Boot)
