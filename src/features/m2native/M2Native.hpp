// Native modern-M2 (MD21) reader: public entry points and the query surface for stats and status.
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

/// Read-only query surface plus the load entry point of the native modern-M2 reader
/// (features/m2native): the client reads a Legion-window MD21 container (inner version 272-274)
/// directly, direct-filling the stock CM2Shared runtime -- no host transform, no in-memory
/// MD21->MD20 reshape, no version rewrite (the resident header keeps its modern version).
/// Everything here is snapshot-by-value so the Lua methods (wxl.m2.*) never hold pointers into
/// the feature's mutable state.
namespace wxl::runtime::m2native
{
    /** @brief Session counters of the native MD21 reader (all monotonic). */
    struct Stats
    {
        uint32_t modelsNative;       ///< MD21 models direct-filled through the native path
        uint32_t modelsFailed;       ///< MD21 models the native fill rejected (corrupt / OOB / fault)
        uint32_t texturesResolved;   ///< TXID FileDataIDs resolved to a client path (handle created)
        uint32_t texturesUnresolved; ///< TXID FileDataIDs the host DB2 authority could not resolve
        uint32_t skippedCameras;     ///< models whose modern-stride camera records were parked (Phase 2)
        uint32_t skippedParticles;   ///< models whose modern-stride particle emitters were parked (Phase 2)
        uint32_t skippedTxac;        ///< models carrying a TXAC chunk (no 3.3.5 home; logged skip)
        uint32_t skippedLdv1;        ///< models carrying LDV1 LOD-skin data (profile 0 only in Phase 1)
        uint32_t skippedAfid;        ///< models carrying AFID (external .anim ids; Phase 2)
        uint32_t skippedSkid;        ///< models carrying SKID (.skel skeleton; Phase 3 -- load refused)
        uint32_t skippedOtherChunks; ///< models carrying any other auxiliary chunk (EXP2, PFDC, ...)
        uint32_t externalSeqPending; ///< sequences seen whose data streams from a .anim file (Phase 2)
        /// Models whose CM2Shared+0x198 shadow-animate gate was zero and had to be lifted to 1.
        /// A modern M2 ships every bone flag at 0x0, which would send the shadow pass down an animate
        /// fast path that never refreshes the bone palette -- the cause of shadows tracking the camera.
        uint32_t shadowGateForced;
    };

    /** @brief Returns a snapshot of the session counters. */
    Stats GetStats();

    /** @brief True when the native reader is compiled in (config.hpp kNativeM2). */
    bool Enabled();

    /**
     * @brief Reports whether a model's resident load buffer is an MD21 chunked container.
     * @param model  runtime model object (buffer at +0x150, size at +0x16C).
     * @return true when the buffer starts with the 'MD21' chunk-container magic.
     */
    bool IsModernContainer(void* model);

    /**
     * @brief Native direct-fill of the stock CM2Shared runtime from the resident MD21 container.
     *
     * Runs in place of the stock parser (never calls it): demuxes the container, drives the stock
     * per-field offset->pointer walk over the modern body at its native strides, resolves TXID
     * texture FileDataIDs, and finishes the runtime exactly as the stock parser would (texture
     * handles at +0x174, skin profile via the stock name-based loader, loaded flag). SEH-guarded:
     * malformed data becomes a logged failure, never a crash.
     * @param model  runtime model whose buffer holds the raw MD21 bytes.
     * @return 1 on success, 0 on failure -- the stock parser's own result contract.
     */
    int NativeLoad(void* model);
}
