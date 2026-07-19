// Native split-ADT reader (Cata+ root/_tex0/_obj0 tiles): public query surface for stats and status.
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

/// Read-only query surface over the split-ADT reader (features/adtsplit): per-map split detection
/// state, session counters, and per-resident-tile status. Everything is snapshot-by-value so the
/// Lua methods (wxl.adt.*) never hold pointers into the feature's mutable state.
namespace wxl::runtime::adtsplit
{
    /** @brief Session counters for the split-ADT reader (all monotonic except tilesResident). */
    struct Stats
    {
        uint32_t splitMapsDetected; ///< maps whose _tex0 probe answered "split"
        uint32_t tilesLoaded;       ///< split tiles that completed their 3-file load
        uint32_t tilesResident;     ///< split tiles currently alive
        uint32_t chunksFilled;      ///< CMapChunks direct-filled from split buffers
        uint32_t mcrfBytes;         ///< bytes materialized for the MCRD‖MCRW -> MCRF concat fixups
        uint32_t parkedMtxpTiles;   ///< tiles whose MTXP (height texturing params) is parked
        uint32_t parkedMclvChunks;  ///< chunks whose MCLV (baked light) is parked
        uint32_t parkedHoleChunks;  ///< chunks whose original high-res hole mask is parked
        uint32_t loadFailures;      ///< split loads that fell back / failed to parse
        uint32_t wdlRead;           ///< Cata+ WDLs read directly into the stock low-detail runtime
        uint32_t heightTexLoaded;   ///< "_h" height texture handles created for height-blend tiles
    };

    /** @brief Returns a snapshot of the session counters. */
    Stats GetStats();

    /**
     * @brief Reports the cached split-detection state of the CURRENT map.
     * @return 1 when the map is split (Cata+ trio), 0 when monolithic, -1 when not yet probed
     *         (no tile of this map has been requested) or no map is loaded.
     */
    int IsSplitMap();

    /** @brief Status of one resident split tile. */
    struct TileStatus
    {
        int      tileFirst;        ///< first  %d of "<Map>_%d_%d.adt"
        int      tileSecond;       ///< second %d of "<Map>_%d_%d.adt"
        uint32_t rootSize;         ///< resident root buffer bytes
        uint32_t texSize;          ///< resident _tex0 buffer bytes (0 = missing)
        uint32_t objSize;          ///< resident _obj0 buffer bytes (0 = missing)
        uint32_t chunkCount;       ///< MCNKs indexed from the root (256 on a well-formed tile)
        bool     complete;         ///< the 3-file load + stock parse finished
        bool     hasMtxp;          ///< a parked MTXP block exists for this tile
        uint32_t mclvChunks;       ///< chunks with parked MCLV
        uint32_t hiResHoleChunks;  ///< chunks with parked high-res holes
    };

    /** @brief Returns the number of resident split tiles. */
    uint32_t ResidentTileCount();

    /**
     * @brief Fetches a resident split tile's status by enumeration index (0..ResidentTileCount()-1).
     *        Enumeration order is unspecified and may change as tiles stream in/out.
     * @return true when the index was valid and out was filled.
     */
    bool GetResidentTile(uint32_t index, TileStatus& out);

    /**
     * @brief Finds a resident split tile by its filename indices.
     * @return true when the tile is resident and out was filled.
     */
    bool FindTile(int tileFirst, int tileSecond, TileStatus& out);

    // --- height-blend data surface (consumed by features/adtsplit/HeightBlend.cpp, draw thread) ---

    /** @brief One terrain layer's height-blend inputs, resolved from MTXP + MHID / "_h.blp". */
    struct HeightLayer
    {
        void*    texture;      ///< CGxTex* "_h" handle; null = use the neutral solid-white texture
        float    heightScale;  ///< MTXP heightScale (0.0 when the record is default/absent)
        float    heightOffset; ///< MTXP heightOffset (1.0 when the record is default/absent)
        uint32_t tilingExp;    ///< MTXP/MTXF flags bits 4..7: layer UV scale divides by (1 << exp)
    };

    /**
     * @brief Resolves the height-blend inputs of one layer of a resident split tile.
     *
     * area is the CMapArea* the drawn chunk belongs to; textureId is the layer record's
     * MCLY.textureId (index into the tile texture set = MTXP/MHID order). On first use per tile the
     * per-texture slot table is built lazily: MTXP record (fallback {MTXF flags, 0, 1}), and -- for
     * a non-default record without flag 0x1 -- the "_h" texture created via MHID FileDataID
     * (fdid::ResolveTexture) or the "<diffuse>_h.blp" name, through the by-name map texture loader.
     * Must be called on the main/draw thread (same thread class as tile load/teardown).
     *
     * @return false when the tile is not a completed split tile or carries no MTXP (caller must run
     *         the untouched stock draw); true with out filled otherwise (texture may be null=white).
     */
    bool GetHeightLayer(void* area, uint32_t textureId, HeightLayer& out);

    /** @brief Lock-free resident split-tile count (relaxed) for per-draw fast-path gating. */
    uint32_t ResidentTilesRelaxed();
}
