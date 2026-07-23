// Native modern-WMO reader: tag-driven chunk walkers replacing the client's positional ones.
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
 * @brief Read-only query surface of the native modern-WMO reader (features/wmonative).
 *
 * The 3.3.5 root and group chunk walkers are POSITIONAL: they skip MVER blind, then consume a fixed
 * sequence of chunks, storing a content pointer and a count derived from `chunkSize / stride`, without
 * ever comparing a FourCC (the sole exception is the optional trailing MCVP). They also carry no bounds
 * check, no validation and no early return, so a modern root -- which drops MOTX/MODN/MOSB/MOVV/MOVB and
 * inserts GFID/MODI/MGI2/MDDI/MNLD/MFED/MAVG -- desynchronises every pointer that follows, silently.
 *
 * This feature replaces the three walkers with tag-driven ones that write exactly the same runtime
 * fields, so a modern WMO is read WHERE IT LIES: no chunk is reordered, no buffer is rebuilt, no
 * "3.3.5-shaped" file is synthesized. A stock WMO is classified in one cheap pass and handed to the
 * untouched original walker, so mixed scenes stay correct.
 *
 * Faithfulness note: the stock loader itself writes into the loaded file buffer (MOGI sky-flag clear,
 * MOPT NaN plane repair, MOCV alpha rewrite, MOMT texture handles). Those are the client's own
 * semantics on its own working memory, and the replacement walkers reproduce them exactly.
 */
namespace wxl::runtime::wmonative
{
    /** @brief Session counters of the native WMO reader (all monotonic). */
    struct Stats
    {
        uint32_t rootsModern;        ///< roots classified modern and walked by the tag-driven walker
        uint32_t rootsStock;         ///< roots handed to the untouched original walker
        uint32_t groupsModern;       ///< groups walked by the tag-driven walker
        uint32_t groupsFailed;       ///< groups whose walk faulted (SEH) -- left empty, never a crash
        uint32_t texturesResolved;   ///< MOMT texture FileDataIDs resolved to a client path
        uint32_t texturesUnresolved; ///< MOMT texture FileDataIDs the DB2 authority could not resolve
        uint32_t parkedDoodadDefs;   ///< roots whose MODD was parked (MODI index form, no MODN blob)
        uint32_t parkedLiquids;      ///< groups whose MLIQ was parked (modern liquid ids crash the client)
        uint32_t parkedIndex32;      ///< groups carrying MOVX (32-bit indices) with no 3.3.5 home
        uint32_t unknownChunks;      ///< modern-only chunks seen and skipped (GFID, MGI2, MNLD, ...)
        /// Materials whose modern shader id was remapped onto the nearest native effect (0..6) by
        /// combiner family, split by target below. The env reflection / emissive glow that native 5/6
        /// cannot express is the remaining fidelity gap and the custom-shader follow-up.
        uint32_t shaderRemapped;     ///< total materials remapped (sum of the three below)
        uint32_t shaderToTwoLayer;   ///< -> native 6 TwoLayerDiffuse (second diffuse layer kept)
        uint32_t shaderToEnv;        ///< -> native 5 EnvMetal (base + env kept, emissive dropped)
        uint32_t shaderToSingle;     ///< -> native 0 Diffuse (single layer)
        /// Collision-only groups (geometry, no MOTV/MONR, no batches) whose runtime vertex count was
        /// zeroed so the stock vertex fill -- which reads those arrays unconditionally -- skips them.
        uint32_t parkedNoUvGroups;
        /// MOBA batches whose modern u16 material index was copied into the byte this client reads.
        /// Without it every batch draws material 0 -- one texture for a whole building.
        uint32_t materialIdsMoved;
        uint32_t materialIdsOutOfRange; ///< indices above 255, unreachable by the client's `movzx byte`
        /// Per-batch AABB culls bypassed because the batch is modern and ships no box (cumulative, per
        /// frame). Group-level frustum / portal / horizon culling still applies.
        uint32_t batchCullBypassed;
        /// Modern groups whose MOCV was run through the client's CPU vertex-colour fix. A modern WMO's
        /// near-black MOCV multiplied into the texture renders black; the fix is what 3.3.5 does for its
        /// own WMOs and the flag disabling it assumes a shader 3.3.5 does not have. See VertexColorFix*.
        uint32_t vertexColorFixed;
        /// MOCV entries zeroed as near-white placeholders (ex-hot-convert parity): a modern exterior group
        /// left near-white renders almost white on 3.3.5 (which multiplies the surface by the vertex colour);
        /// zeroing lets it shade from the ambient/scene light like the correctly-baked groups.
        uint32_t mocvNeutralized;
        uint32_t uvTransformed; ///< groups whose UV set was transformed by the experimental UvMode probe
        /// Pure-single-layer 2+-set groups whose base UV slot was pointed at the group's LAST set at load,
        /// so their single-layer batches sample set1 with no shader/declaration dependency (Legion's "last
        /// N UV sets" rule). Groups that also carry a Composite batch are left to the per-batch VS route.
        uint32_t baseSetReoriented;
    };

    /** @brief Returns a snapshot of the session counters. */
    Stats GetStats();

    /** @brief True when the native reader is compiled in (config.hpp kNativeWmo). */
    bool Enabled();

    /** @brief True when every walker detour installed; false leaves the client fully stock. */
    bool Installed();

    /**
     * @brief Reports whether a loaded root was classified as a modern container.
     * @param root  map-object root object.
     * @return true when its buffer carries no MOTX or carries GFID.
     */
    bool IsModernRoot(void* root);

    /** @brief Live A/B of the modern-shader family remap. False reverts to the single-layer fallback. */
    bool ShaderRemapEnabled();
    void SetShaderRemapEnabled(bool on);

    /**
     * @brief Live A/B of the modern vertex-colour fix.
     *
     * True (default) runs the client's CPU MOCV fix on modern groups even though they set MOHD 0x08
     * (do_not_fix), because that flag assumes a unified shader 3.3.5 lacks -- without it the near-black
     * modern MOCV multiplied into the texture renders every surface black. False honours the flag (stock
     * behaviour) so the black can be seen come back.
     */
    bool VertexColorFixEnabled();
    void SetVertexColorFixEnabled(bool on);

    /**
     * @brief Live A/B of the near-white MOCV neutralization (ex-hot-convert parity).
     *
     * True (default) zeroes the RGB of any near-white vertex colour so an over-bright modern exterior group
     * shades from the ambient/scene light instead of rendering almost white. False leaves MOCV as-is.
     */
    bool NearWhiteNeutralizeEnabled();
    void SetNearWhiteNeutralizeEnabled(bool on);

    /**
     * @brief EXPERIMENTAL UV-orientation probe. 0 = untouched; 1 swap u/v; 2 rot+90; 3 rot-90; 4 flip v;
     *        5 flip u; 6 feed the second UV set to the base texture. Applied at group load, so a change
     *        takes effect on the next (re)load of a WMO -- walk away and back. Finds the transform that
     *        de-rotates modern WMO textures empirically before it is made permanent / RE-confirmed.
     */
    int  UvMode();
    void SetUvMode(int mode);
}
