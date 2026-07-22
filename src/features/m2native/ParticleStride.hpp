// Teaches the client to step modern M2 particle emitter records, and to read their zSource sentinel.
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

/**
 * @brief Makes the client able to read a modern M2's particle emitters where they lie.
 *
 * The record grew from 0x1DC to 0x1EC bytes, but every field below 0x1DC kept its offset -- the 16
 * extra bytes are appended at the end. So nothing about the data needs changing; the client simply
 * has to step the right distance between records. Nine instructions hardcode that step and each is
 * redirected at a thunk that derives the stride from the model being processed, which keeps a scene
 * mixing stock v264 and modern doodads correct.
 *
 * Separately, this neutralizes the modern `zSource = 255.0` sentinel at the single client function
 * that consumes it. Modern content means "no point source" by it; the client tests `== 0.0` and so
 * takes the point-source branch, discarding the emission cone and overwriting the spawn position.
 * That -- not the record stride -- is why modern fire drifts sideways instead of rising.
 */
namespace wxl::runtime::m2particles
{
    struct Stats
    {
        uint32_t sitesPatched;       ///< stride sites successfully redirected (all-or-nothing: 9 or 0)
        uint32_t zSourceNeutralized; ///< SetZsource calls whose 255.0 sentinel was rewritten to 0
    };

    /** @brief True when every stride site was patched. */
    bool Installed();

    /**
     * @brief True once the client has been taught to unpack a modern packed multi-texture textureId
     *        at its read sites -- the remaining precondition for un-parking emitters.
     *
     * With flag 0x10000000 the textureId at record+0x16 holds three packed 5-bit ids. The stock client
     * uses the raw value as a flat index into the texture-handle table, so InitializeLoaded
     * (0x00833ED5 -> table[id] -> SetMaterial) and ReplaceTexture (0x00825349) both read out of bounds
     * and fault. The fix is to patch those READ sites to unpack when the flag is set, leaving the
     * record exactly as the file has it -- the same pattern as the stride thunks. Each site replaces
     * ONE COMPLETE INSTRUCTION, which is what makes it safe without a full disassembly: a branch to
     * that instruction's address still lands on our call, and a branch into an instruction's interior
     * cannot exist in valid code.
     */
    bool TextureIdSitesPatched();

    /** @brief Live toggle for the zSource sentinel rewrite, so it can be A/B'd against stock. */
    bool ZSourceFixEnabled();
    void SetZSourceFixEnabled(bool on);

    Stats GetStats();
}
