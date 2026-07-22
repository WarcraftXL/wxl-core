// Native split-ADT reader: loads Cata+ root/_tex0/_obj0 tiles and DIRECT-FILLS the stock CMapChunk.
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

// MECHANISM (direct-fill, no monolithic merge):
//
//   Detection   Once per map, the first tile load probes "<Map>_%d_%d_tex0.adt" through the detoured
//               SFile open (host + archives both answer) and caches the boolean per map prefix. A
//               non-split map takes the stock path untouched from then on (one hash lookup per tile).
//
//   Fetch       CMapArea::Load (0x007D7150) is detoured. For a split tile it mimics the native body
//               (open -> size -> CMap::AllocRawAreaData -> CAsyncObject at area+0x70 -> enqueue) but
//               CHAINS three whole-file async reads -- root, then _tex0, then _obj0 -- each into its
//               own resident buffer, with our completion callback in async+0x10. Exactly ONE async
//               object is in flight per tile at any moment and it always sits at area+0x70, so the
//               stock purge guard, the purge-time AsyncFileReadCancel (incl. its deferred-cleanup
//               buffer handoff), and the non-streaming sync-wait all keep working unmodified. All
//               I/O stays on the existing disk-queue worker threads; completions run on the main
//               thread via the stock drain.
//
//   Residency   The ROOT buffer lives at area+0x80/+0x84 exactly like stock (freed by the stock
//               CMapArea destructor). The _tex0/_obj0 buffers are allocated with the same
//               CMap::AllocRawAreaData and owned by a per-area side record (SplitTile) that a detour
//               on the CMapArea destructor (0x007D6E10) releases AFTER the original runs -- i.e. all
//               three buffers live exactly as long as the tile, so every CMapChunk sub-chunk pointer
//               into them stays valid for the chunk's whole life.
//
//   Fill        After the third read, the finalizer (main thread, same slot where the stock parser
//               would run) linearly walks the three files ONCE (headers only, no payload copies),
//               builds the per-chunk pointer index, applies the tiny structural fixups IN PLACE
//               (root MCNK header counts, high-res hole collapse, has_mcsh flag), materializes the
//               only payload fixup the format demands (MCRF = MCRD data ‖ MCRW data, tens of KB),
//               synthesizes a 4 KB MCIN + the root MHDR offset patch, and then calls the UNCHANGED
//               stock CMapArea::Create (0x007D6EF0) -- so MTEX textures, MDDF/MODF tables, MH2O
//               liquid and MFBO all wire up through the stock code. Per chunk, a detour on
//               CMapChunk::ProcessIffChunks (0x007C3A10) replaces the sequential sub-chunk walk with
//               direct pointer assignment into the three resident buffers (root: MCVT/MCCV/MCNR/
//               MCLQ/MCSE, _tex0: MCLY/MCAL/MCSH, _obj0: MCRF fixup). Everything downstream
//               (CMapChunk::Create, bounds, liquids, refs, render nodes, purge) is stock.
//
//   Parked      Chunks 3.3.5 cannot represent are parsed and RETAINED, not rendered: MTXP (pointer
//               into the resident _tex0 buffer), MCLV (pointers into the resident root buffer),
//               original high-res hole masks (copied u64s), MHDR/MAMP amplitude byte. A later
//               feature can light these up without re-reading the files.
// (CMapArea__Load/Create/destructor, ProcessIffChunks,
// AsyncFileReadAllocObject/DestroyObject/Cancel, CMap__AllocRawAreaData/FreeRawAreaData).

#include "config.hpp"
#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"
#include "engine/events/Event.hpp"
#include "features/adtsplit/AdtSplit.hpp"
#include "features/fdid/Fdid.hpp"

#include "common/Log.hpp"
#include "game/Binding.hpp"
#include "offsets/engine/Io.hpp"
#include "offsets/game/ADT.hpp"
#include "offsets/game/World.hpp"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    namespace ev  = wxl::events;
    namespace adt = wxl::offsets::game::adt;
    namespace wld = wxl::offsets::game::world;
    namespace io  = wxl::offsets::engine::io;

    // ---------------------------------------------------------------- raw byte helpers
    inline uint32_t Rd32(const void* p)             { uint32_t v; std::memcpy(&v, p, 4); return v; }
    inline uint64_t Rd64(const void* p)             { uint64_t v; std::memcpy(&v, p, 8); return v; }
    inline void     Wr32(void* p, uint32_t v)       { std::memcpy(p, &v, 4); }
    inline void     Wr16(void* p, uint16_t v)       { std::memcpy(p, &v, 2); }

    /// FourCC as the client compares it: chunk tags are stored byte-reversed on disk, so the
    /// little-endian dword read equals ('M'<<24)|('C'<<16)|... for an "MC.." chunk.
    constexpr uint32_t FourCC(const char (&s)[5])
    {
        return (uint32_t(uint8_t(s[0])) << 24) | (uint32_t(uint8_t(s[1])) << 16) |
               (uint32_t(uint8_t(s[2])) << 8)  |  uint32_t(uint8_t(s[3]));
    }

    /// True when the 4 bytes at p read back as an "MC.." sub-chunk tag (disk order is reversed).
    inline bool LooksLikeSubTag(const uint8_t* p) { return p[3] == 'M' && p[2] == 'C'; }

    template <class T>
    inline T& At(void* base, size_t off) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + off); }

    // ---------------------------------------------------------------- native call shims
    // The storage entry points are called BY ADDRESS so the calls land in the installed detours
    // (StorageHook serves host files through them), same trick Phasing uses for its WDT probe.
    void* OpenFile(const char* name)
    {
        void* h = nullptr;
        if (!reinterpret_cast<io::Storage_FileOpenFn>(io::kFileOpen)(nullptr, name, 0, &h))
            return nullptr;
        return h;
    }
    uint32_t SizeOfFile(void* h) { return reinterpret_cast<io::Storage_FileSizeFn>(io::kFileSize)(h, nullptr); }
    void     CloseFile(void* h)  { reinterpret_cast<io::Storage_FileCloseFn>(io::kFileClose)(h); }
    bool     ReadBytes(void* h, void* dst, uint32_t len)
    {
        return reinterpret_cast<io::Storage_FileReadFn>(io::kFileRead)(h, dst, len, nullptr, nullptr, 0) != 0;
    }

    uint8_t* AllocRaw(uint32_t size)
    {
        return static_cast<uint8_t*>(wxl::game::Native<adt::AllocRawAreaDataFn>(adt::kAllocRawAreaData)(size));
    }
    void FreeRaw(void* p, uint32_t size)
    {
        wxl::game::Native<adt::FreeRawAreaDataFn>(adt::kFreeRawAreaData)(p, size);
    }
    void* AsyncAlloc()
    {
        return wxl::game::Native<wld::AsyncFileReadAllocObjectFn>(wld::kAsyncFileReadAllocObject)();
    }
    // Enqueue by address: streaming's round-robin detour on AsyncFileReadObject receives the call.
    void AsyncEnqueue(void* obj)
    {
        reinterpret_cast<wld::AsyncFileReadObjectFn>(wld::kAsyncFileReadObject)(obj, 0);
    }
    // AsyncFileReadDestroyObject: spin-waits an in-service read, unlinks a queued completion so it
    // can never run, closes the object's file handle and recycles the node.
    void AsyncRetire(void* obj)
    {
        reinterpret_cast<wld::AsyncDestroyFn>(wld::kAsyncDestroy)(obj);
    }

    // ---------------------------------------------------------------- state
    enum class Stage : int { Root = 0, Tex = 1, Obj = 2, Done = 3 };

    /// Per-chunk direct-fill index: every pointer aims INTO one of the three resident buffers,
    /// except mcrf which may aim into the tile's small MCRF concat pool.
    struct ChunkFill
    {
        uint8_t* rootMcnkHdr = nullptr; // raw MCNK (tag+size) in the root buffer
        // root-file sub-chunks
        uint8_t* mcvt = nullptr;
        uint8_t* mccv = nullptr;
        uint8_t* mcnr = nullptr;
        uint8_t* mclq = nullptr; uint32_t mclqSize = 0;
        uint8_t* mcse = nullptr; uint32_t mcseSize = 0;
        // _tex0 sub-chunks (header-less MCNK, correspondence by order)
        uint8_t* mcly = nullptr; uint32_t mclySize = 0;
        uint8_t* mcal = nullptr; uint32_t mcalSize = 0;
        uint8_t* mcsh = nullptr; uint32_t mcshSize = 0;
        // _obj0 sub-chunks
        uint8_t* mcrd = nullptr; uint32_t mcrdSize = 0;
        uint8_t* mcrw = nullptr; uint32_t mcrwSize = 0;
        // materialized fixup (MCRD data ‖ MCRW data) or a direct alias when one side is empty
        uint8_t* mcrf = nullptr;
        uint32_t nDoodadRefs = 0;
        uint32_t nMapObjRefs = 0;
        // parked (no 3.3.5 runtime home; retained for later features)
        uint8_t* mclv = nullptr;
        uint64_t hiResHoles = 0;
        bool     hadHiResHoles = false;
    };

    /// Per-tile side record. Owns the _tex0/_obj0 buffers (root is owned by the area at +0x80) and
    /// every small fixup allocation the direct-filled chunks point at. Lifetime: created in the
    /// CMapArea::Load detour, released in the CMapArea::destructor detour AFTER the original body,
    /// i.e. after every chunk of the tile is already purged -- nothing can dangle.
    struct SplitTile
    {
        void*    area = nullptr;
        int      tileFirst = 0, tileSecond = 0;
        char     texName[300] = { 0 };
        char     objName[300] = { 0 };
        Stage    stage    = Stage::Root;
        bool     complete = false;
        void*    curAsync = nullptr;

        uint8_t* rootBuf = nullptr; uint32_t rootSize = 0; // aliased by area+0x80/+0x84
        uint8_t* texBuf  = nullptr; uint32_t texSize  = 0; bool texOwned = false;
        uint8_t* objBuf  = nullptr; uint32_t objSize  = 0; bool objOwned = false;

        ChunkFill chunks[256];
        uint32_t  chunkCount = 0;

        std::vector<uint8_t> mcin;       // synthesized MCIN chunk (8-byte header + 256 x 16 B)
        std::vector<uint8_t> mcrfPool;   // concatenated MCRD‖MCRW payloads (chunks alias into it)
        std::vector<uint8_t> synthEmpty; // synthesized empty top-level chunks for absent tables
        // Real MMDX/MMID name-table chunks synthesized from the doodads' MDDF FileDataID nameIds
        // (resolved via ModelFilePath). Each is a full {tag,size,data} chunk; MHDR points at them and
        // the (in-place rewritten) MDDF entries index MMID. Owned for the tile lifetime.
        std::vector<uint8_t> mmdxChunk;  // "MMDX" + NUL-terminated model paths
        std::vector<uint8_t> mmidChunk;  // "MMID" + u32 offsets into the MMDX payload

        // Texture FileDataIDs read from _tex0 MDID (diffuse), indexed by MCLY.textureId. Legion+ tiles
        // carry no MTEX; these are how the terrain layers name their textures. The tex manager detour
        // resolves each through TextureFilePath.db2 into texNameBlob (NUL-terminated client paths, in
        // MDID order) and feeds THAT to the stock CMapArea::LoadTextures -- the slots then point into
        // this persistent blob for the tile lifetime (freed with the SplitTile).
        std::vector<uint32_t> mdid;
        std::vector<char>     texNameBlob;

        // parked tile-level foreigners
        uint8_t* mtxp = nullptr; uint32_t mtxpSize = 0;    // into the resident _tex0 buffer
        uint8_t  mampValue = 0;                            // MHDR+0x30 / MAMP byte (zeroed for 3.3.5)
        uint32_t mclvChunks = 0, hiResHoleChunks = 0;

        // Height-blend inputs. mhid mirrors mdid (index-aligned
        // height-texture FileDataIDs); mtxfData/mtexData park the _tex0 MTXF flag table and MTEX
        // name blob (both point into the resident _tex0 buffer) for the MTXP-default flag fallback
        // and the name-based "_h.blp" derivation on tiles without FileDataIDs.
        std::vector<uint32_t> mhid;
        uint8_t* mtxfData = nullptr; uint32_t mtxfSize = 0;
        uint8_t* mtexData = nullptr; uint32_t mtexSizeParked = 0;
        /// Per-texture height slot, MTXP/MDID order. tex==null means "solid white" (default params,
        /// flag 0x1, or an unresolvable file). owned marks handles this record must release. exp is
        /// the UV-tiling exponent from the record's flags bits 4..7 (applies to the diffuse layer
        /// whether or not a height texture exists).
        struct HeightSlot
        {
            void* tex = nullptr; bool owned = false;
            float scale = 0.0f; float offset = 1.0f; uint8_t exp = 0;
        };
        std::vector<HeightSlot> heightSlots;
        bool heightBuilt = false;
    };

    std::mutex g_mutex; // guards the two maps below (loads are main-thread; Lua reads snapshot)
    std::unordered_map<void*, std::unique_ptr<SplitTile>> g_tiles;   // key: CMapArea*
    std::unordered_map<std::string, bool> g_splitMaps;               // key: "<dir>\<name>" prefix

    std::atomic<uint32_t> g_statSplitMaps{ 0 }, g_statTilesLoaded{ 0 }, g_statTilesResident{ 0 },
        g_statChunksFilled{ 0 }, g_statMcrfBytes{ 0 }, g_statMtxpTiles{ 0 },
        g_statMclvChunks{ 0 }, g_statHoleChunks{ 0 }, g_statFailures{ 0 },
        g_statWdlRead{ 0 }, g_statHeightTex{ 0 }, g_statDoodadModels{ 0 };

    adt::TileAreaLoadFn            g_origTileAreaLoad       = nullptr;
    adt::ChunkProcessIffChunksFn  g_origProcessIff         = nullptr;
    adt::TileAreaDestroyFn        g_origTileAreaDestroy    = nullptr;
    adt::LoadWdlFn                g_origLoadWdl            = nullptr;
    adt::Map_AreaLoadTexturesFn   g_origAreaLoadTextures   = nullptr;
    adt::Map_LoadTerrainTextureFn g_origLoadTerrainTexture = nullptr;

    SplitTile* FindTileLocked(void* area)
    {
        auto it = g_tiles.find(area);
        return it != g_tiles.end() ? it->second.get() : nullptr;
    }

    // Load-thread lookup that takes g_mutex only for the map access, then releases it: the texture
    // detours below call the stock originals while NOT holding the lock (the eager path re-enters
    // CMap::LoadTerrainTexture, which would deadlock a non-recursive mutex). Safe because a tile is
    // only ever erased on this same load thread (CMapArea destructor), so the pointer cannot dangle.
    SplitTile* FindTileBrief(void* area)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return FindTileLocked(area);
    }

    // ---------------------------------------------------------------- split detection
    /**
     * @brief Derives the per-map cache key from a tile filename.
     *
     * "<dir>\<name>_%d_%d.adt" -> "<dir>\<name>". Returns false when the name does not look like a
     * tile path (no ".adt" tail or no two trailing _<digits> groups) -- such a name is never split.
     */
    bool MapKeyFromTileName(const char* name, std::string& key)
    {
        const size_t len = name ? std::strlen(name) : 0;
        if (len < 9) return false; // "a_0_0.adt" is the shortest plausible
        const char* ext = name + len - 4;
        if (_stricmp(ext, ".adt") != 0) return false;

        // scan back across two _<digits> groups
        const char* p = ext;
        for (int group = 0; group < 2; ++group)
        {
            const char* d = p;
            while (d > name && d[-1] >= '0' && d[-1] <= '9') --d;
            if (d == p || d == name || d[-1] != '_') return false;
            p = d - 1;
        }
        key.assign(name, static_cast<size_t>(p - name));
        return true;
    }

    /**
     * @brief One-time MPHD compatibility fix for split maps: Legion-side `adt_has_height_texturing`
     *        (0x80) implies 4096-byte alpha maps, which 3.3.5's unpack sites size from bit2 only --
     *        so bit2 is OR'd in when 0x80 is set without it. The flags dword is the live WDT MPHD
     *        copy the alpha unpackers consult at every build.
     */
    void ApplyMphdAlphaFix()
    {
        uint32_t& flags = *reinterpret_cast<uint32_t*>(adt::kMphdFlags);
        if ((flags & 0x80u) != 0 && (flags & 0x4u) == 0)
        {
            flags |= 0x4u;
            WLOG_INFO("adt-split: MPHD height-texturing flag present, forcing big-alpha (bit2) for unpack sizing");
        }
    }

    /**
     * @brief Reports (and lazily probes) whether the map a tile filename belongs to is split.
     *
     * The probe opens "<prefix>_%d_%d_tex0.adt" of THIS tile once through the storage seam and
     * caches the answer per map prefix -- an all-or-nothing-per-map dataset assumption, matching how
     * repacks ship. Cost: one open/close per map per session.
     */
    bool IsSplitTileName(const char* name, std::string& keyOut)
    {
        if (!MapKeyFromTileName(name, keyOut)) return false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_splitMaps.find(keyOut);
            if (it != g_splitMaps.end()) return it->second;
        }
        const size_t len = std::strlen(name);
        std::string probe(name, len - 4);
        probe += "_tex0.adt";
        void* h = OpenFile(probe.c_str());
        const bool split = h != nullptr;
        if (h) CloseFile(h);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_splitMaps[keyOut] = split;
        }
        if (split)
        {
            g_statSplitMaps.fetch_add(1, std::memory_order_relaxed);
            ApplyMphdAlphaFix();
            WLOG_INFO("adt-split: map '%s' detected as SPLIT (Cata+ root/_tex0/_obj0)", keyOut.c_str());
        }
        return split;
    }

    // ---------------------------------------------------------------- parse + fixups
    /// Generic bounds-checked top-level chunk walk; cb(tag, chunkHeaderPtr, dataSize).
    template <class Fn>
    void WalkTop(uint8_t* buf, uint32_t size, Fn&& cb)
    {
        uint32_t off = 0;
        while (off + 8 <= size)
        {
            const uint32_t tag = Rd32(buf + off);
            const uint32_t sz  = Rd32(buf + off + 4);
            if (sz > size - off - 8) break;
            cb(tag, buf + off, sz);
            off += 8 + sz;
        }
    }

    /// Indexes one ROOT MCNK (0x80-byte SMChunk header + sub-chunks) into the chunk's fill record.
    void WalkRootMcnk(ChunkFill& f, uint8_t* hdr, uint32_t chunkSize)
    {
        f.rootMcnkHdr = hdr;
        if (chunkSize < 0x80) return;
        uint8_t* p   = hdr + 8 + 0x80;
        uint32_t rem = chunkSize - 0x80;
        while (rem >= 8)
        {
            const uint32_t tag = Rd32(p);
            const uint32_t sz  = Rd32(p + 4);
            if (sz > rem - 8) break;
            uint8_t* payload = p + 8;
            switch (tag)
            {
            case FourCC("MCVT"): f.mcvt = payload; break;
            case FourCC("MCCV"): f.mccv = payload; break;
            case FourCC("MCLV"): f.mclv = payload; break;      // parked (Cata+ baked light)
            case FourCC("MCNR"): f.mcnr = payload; break;
            case FourCC("MCLQ"): f.mclq = payload; f.mclqSize = sz; break;
            case FourCC("MCSE"): f.mcse = payload; f.mcseSize = sz; break;
            default: break; // MCDD/MCBB/... silently skipped
            }
            uint32_t adv = 8 + sz;
            // Monolithic-era MCNR stores size 0x1B3 with 13 physical pad bytes before the next tag;
            // resync when a split root kept that layout. (Direct-fill itself never needs the pad:
            // no consumer reads past the 435 normal bytes and the stock walker is bypassed.)
            if (tag == FourCC("MCNR") && sz == 0x1B3 && rem >= adv + 13 + 8 &&
                !LooksLikeSubTag(p + adv) && LooksLikeSubTag(p + adv + 13))
                adv += 13;
            if (adv > rem) break;
            p += adv;
            rem -= adv;
        }
    }

    /// Indexes one HEADER-LESS _tex0 MCNK (sub-chunks from data+0).
    void WalkTexMcnk(ChunkFill& f, uint8_t* data, uint32_t size)
    {
        uint32_t off = 0;
        while (off + 8 <= size)
        {
            const uint32_t tag = Rd32(data + off);
            const uint32_t sz  = Rd32(data + off + 4);
            if (sz > size - off - 8) break;
            uint8_t* payload = data + off + 8;
            switch (tag)
            {
            case FourCC("MCLY"): f.mcly = payload; f.mclySize = sz; break;
            case FourCC("MCAL"): f.mcal = payload; f.mcalSize = sz; break;
            case FourCC("MCSH"): f.mcsh = payload; f.mcshSize = sz; break;
            default: break; // MCMT/MAMP-level oddities skipped
            }
            off += 8 + sz;
        }
    }

    /// Indexes one HEADER-LESS _obj0 MCNK (sub-chunks from data+0).
    void WalkObjMcnk(ChunkFill& f, uint8_t* data, uint32_t size)
    {
        uint32_t off = 0;
        while (off + 8 <= size)
        {
            const uint32_t tag = Rd32(data + off);
            const uint32_t sz  = Rd32(data + off + 4);
            if (sz > size - off - 8) break;
            uint8_t* payload = data + off + 8;
            switch (tag)
            {
            case FourCC("MCRD"): f.mcrd = payload; f.mcrdSize = sz; break;
            case FourCC("MCRW"): f.mcrw = payload; f.mcrwSize = sz; break;
            default: break; // MCBB etc. skipped
            }
            off += 8 + sz;
        }
    }

    /// Collapses the Cata+ 8x8-bit high-res hole map to the stock 4x4-bit u16 (a low cell is holed
    /// when ANY of its 2x2 high bits is).
    uint16_t CollapseHoles(uint64_t hi)
    {
        uint16_t low = 0;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
            {
                const int hr = r * 2, hc = c * 2;
                const uint64_t mask = (3ull << (hr * 8 + hc)) | (3ull << ((hr + 1) * 8 + hc));
                if (hi & mask) low |= uint16_t(1u << (r * 4 + c));
            }
        return low;
    }

    /**
     * @brief In-place fixups of one root MCNK 0x80-byte header so the stock consumers read true
     *        counts: nLayers/nDoodadRefs/nMapObjRefs from the walked split sizes, sizeAlpha/
     *        sizeLiquid/nSndEmitters normalized, has_mcsh matched to actual MCSH presence, and
     *        high-res holes collapsed to the u16 the index build masks live (the u64 overlaps the
     *        dead ofsHeight/ofsNormal fields and is zeroed after parking).
     */
    void FixChunkHeader(SplitTile& t, ChunkFill& f)
    {
        uint8_t* h = f.rootMcnkHdr + 8;
        uint32_t flags = Rd32(h);
        if (flags & 0x10000u) // high_res_holes
        {
            f.hiResHoles    = Rd64(h + 0x14);
            f.hadHiResHoles = true;
            ++t.hiResHoleChunks;
            Wr16(h + 0x3C, CollapseHoles(f.hiResHoles));
            std::memset(h + 0x14, 0, 8);
            flags &= ~0x10000u;
        }
        if (f.mcsh) flags |= 0x1u; else flags &= ~0x1u;   // has_mcsh gates the shadow unpack deref
        Wr32(h, flags);
        Wr32(h + 0x0C, f.mclySize / 16u);                          // nLayers
        Wr32(h + 0x10, f.nDoodadRefs);                             // nDoodadRefs
        Wr32(h + 0x28, f.mcalSize + 8u);                           // sizeAlpha (stock: data + hdr)
        Wr32(h + 0x2C, f.mcsh ? f.mcshSize + 8u : 8u);             // sizeShadow (unread; hygiene)
        Wr32(h + 0x38, f.nMapObjRefs);                             // nMapObjRefs
        Wr32(h + 0x5C, f.mcse ? f.mcseSize / 0x1Cu : 0u);          // nSndEmitters
        Wr32(h + 0x64, (f.mclq && f.mclqSize) ? f.mclqSize + 8u : 8u); // sizeLiquid (>8 gates MCLQ)
        if (f.mclv) ++t.mclvChunks;
    }

    /**
     * @brief The whole per-tile parse: indexes the three resident buffers, builds the MCRF concat
     *        pool, fixes the 256 root headers in place, synthesizes MCIN (+ empty top-level chunks
     *        for absent tables) and patches the root MHDR offsets so the UNCHANGED stock
     *        CMapArea::Create wires every area field itself.
     * @return false only on catastrophic input (no MVER/MHDR, zero MCNKs).
     */
    bool ParseAndPatch(SplitTile& t)
    {
        uint8_t* rb = t.rootBuf;
        const uint32_t rs = t.rootSize;
        if (!rb || rs < 0x60) return false;
        if (Rd32(rb) != FourCC("MVER")) return false;
        const uint32_t mverSize = Rd32(rb + 4);
        if (mverSize > rs || 8 + mverSize + 8 > rs) return false;
        uint8_t* mhdrHdr = rb + 8 + mverSize;
        if (Rd32(mhdrHdr) != FourCC("MHDR")) return false;
        const uint32_t mhdrSize = Rd32(mhdrHdr + 4);
        if (mhdrSize < 0x34 || (mhdrHdr - rb) + 8ull + mhdrSize > rs) return false;
        uint8_t* mhdr = mhdrHdr + 8;

        // --- index the root ---
        uint8_t* mh2o = nullptr; uint8_t* mfbo = nullptr;
        uint32_t nRoot = 0;
        WalkTop(rb, rs, [&](uint32_t tag, uint8_t* hdr, uint32_t sz) {
            if (tag == FourCC("MCNK")) { if (nRoot < 256) WalkRootMcnk(t.chunks[nRoot], hdr, sz); ++nRoot; }
            else if (tag == FourCC("MH2O")) mh2o = hdr;
            else if (tag == FourCC("MFBO")) mfbo = hdr;
        });
        if (nRoot == 0) return false;
        t.chunkCount = nRoot < 256 ? nRoot : 256;
        if (nRoot != 256)
            WLOG_WARN("adt-split: tile %d_%d root has %u MCNKs (expected 256)", t.tileFirst, t.tileSecond, nRoot);

        // --- index _tex0 ---
        uint8_t* mtex = nullptr; uint8_t* mtxf = nullptr;
        if (t.texBuf)
        {
            uint32_t nTex = 0;
            WalkTop(t.texBuf, t.texSize, [&](uint32_t tag, uint8_t* hdr, uint32_t sz) {
                if (tag == FourCC("MCNK")) { if (nTex < 256) WalkTexMcnk(t.chunks[nTex], hdr + 8, sz); ++nTex; }
                else if (tag == FourCC("MTEX")) { mtex = hdr; t.mtexData = hdr + 8; t.mtexSizeParked = sz; }
                else if (tag == FourCC("MTXF")) { mtxf = hdr; t.mtxfData = hdr + 8; t.mtxfSize = sz; }
                else if (tag == FourCC("MDID"))                                               // texture FileDataIDs
                {
                    const uint32_t n = sz / 4u;
                    t.mdid.resize(n);
                    for (uint32_t i = 0; i < n; ++i) t.mdid[i] = Rd32(hdr + 8 + i * 4);
                }
                else if (tag == FourCC("MHID"))                                               // height FileDataIDs
                {
                    const uint32_t n = sz / 4u;
                    t.mhid.resize(n);
                    for (uint32_t i = 0; i < n; ++i) t.mhid[i] = Rd32(hdr + 8 + i * 4);
                }
                else if (tag == FourCC("MTXP")) { t.mtxp = hdr + 8; t.mtxpSize = sz; }        // height params
                else if (tag == FourCC("MAMP")) { if (sz) t.mampValue = hdr[8]; }             // parked
            });
        }

        // --- index _obj0 ---
        uint8_t* mmdx = nullptr; uint8_t* mmid = nullptr; uint8_t* mwmo = nullptr;
        uint8_t* mwid = nullptr; uint8_t* mddf = nullptr; uint8_t* modf = nullptr;
        if (t.objBuf)
        {
            uint32_t nObj = 0;
            WalkTop(t.objBuf, t.objSize, [&](uint32_t tag, uint8_t* hdr, uint32_t sz) {
                if (tag == FourCC("MCNK")) { if (nObj < 256) WalkObjMcnk(t.chunks[nObj], hdr + 8, sz); ++nObj; }
                else if (tag == FourCC("MMDX")) mmdx = hdr;
                else if (tag == FourCC("MMID")) mmid = hdr;
                else if (tag == FourCC("MWMO")) mwmo = hdr;
                else if (tag == FourCC("MWID")) mwid = hdr;
                else if (tag == FourCC("MDDF")) mddf = hdr;
                else if (tag == FourCC("MODF")) modf = hdr;
            });
        }

        const bool skipWmo     = wxl::features::kAdtSplitSkipWmo;
        const bool skipDoodads = wxl::features::kAdtSplitSkipDoodads;
        if (skipWmo)     { mwmo = nullptr; mwid = nullptr; modf = nullptr; }
        if (skipDoodads) { mmdx = nullptr; mmid = nullptr; mddf = nullptr; }

        // --- doodad FileDataID resolution. A Legion+ _obj0 places doodads by FileDataID: the MDDF entry
        // flag 0x40 means nameId IS the model's FileDataID and there is no MMDX/MMID name table. Resolve
        // each via ModelFilePath into a synthesized MMDX (paths) + MMID (offsets), rewrite every such
        // entry's nameId to its MMID index and clear the 0x40 flag -- so the stock placement path hands
        // the native M2 reader a valid name. Entry COUNT/order is preserved (per-chunk MCRD refs index
        // it); only nameId/flags change. Unresolved ids share one empty-name slot (stock -> ErrorCube).
        if (!skipDoodads && mddf)
        {
            const uint32_t nDood = Rd32(mddf + 4) / 0x24u;
            std::vector<uint8_t>  paths;   // MMDX payload
            std::vector<uint32_t> offs;    // MMID payload (byte offset into paths, one per name slot)
            std::unordered_map<uint32_t, uint32_t> fidToIdx;
            uint32_t unresolvedIdx = 0xFFFFFFFFu, resolved = 0;
            auto intern = [&](const char* p) -> uint32_t {
                const uint32_t idx = static_cast<uint32_t>(offs.size());
                offs.push_back(static_cast<uint32_t>(paths.size()));
                const size_t len = p ? std::strlen(p) : 0;
                paths.insert(paths.end(), p, p + len);
                paths.push_back('\0');
                return idx;
            };
            for (uint32_t i = 0; i < nDood; ++i)
            {
                uint8_t*  e = mddf + 8 + i * 0x24u;
                uint16_t  flags; std::memcpy(&flags, e + 0x22, 2);
                if ((flags & 0x40u) == 0) continue;                 // already a name-table index
                const uint32_t fid = Rd32(e);
                uint32_t idx;
                auto it = fidToIdx.find(fid);
                if (it != fidToIdx.end()) idx = it->second;
                else
                {
                    const char* path = wxl::fdid::ResolveModel(fid);
                    if (path && path[0]) { idx = intern(path); ++resolved; }
                    else { if (unresolvedIdx == 0xFFFFFFFFu) unresolvedIdx = intern(""); idx = unresolvedIdx; }
                    fidToIdx.emplace(fid, idx);
                }
                Wr32(e, idx);                                       // nameId -> MMID index
                flags &= ~0x40u; std::memcpy(e + 0x22, &flags, 2);  // clear the filedata-id flag
            }
            if (!offs.empty())
            {
                t.mmdxChunk.assign(8, 0);
                Wr32(t.mmdxChunk.data(), FourCC("MMDX"));
                Wr32(t.mmdxChunk.data() + 4, static_cast<uint32_t>(paths.size()));
                t.mmdxChunk.insert(t.mmdxChunk.end(), paths.begin(), paths.end());
                t.mmidChunk.assign(8, 0);
                Wr32(t.mmidChunk.data(), FourCC("MMID"));
                Wr32(t.mmidChunk.data() + 4, static_cast<uint32_t>(offs.size() * 4u));
                for (uint32_t o : offs) { uint8_t b[4]; Wr32(b, o); t.mmidChunk.insert(t.mmidChunk.end(), b, b + 4); }
                mmdx = t.mmdxChunk.data();
                mmid = t.mmidChunk.data();
            }
            g_statDoodadModels.fetch_add(resolved, std::memory_order_relaxed);
        }

        // Interim: a Legion+ MH2O uses liquid-type ids beyond 3.3.5's LiquidType.dbc, which the stock
        // liquid-sound query dereferences as a null record (#132 near water). Drop the tile's water by
        // zeroing its MHDR offset -- CMapArea::Create then builds no liquid, so the query finds none.
        if constexpr (wxl::features::kAdtSplitSkipLiquid)
            mh2o = nullptr;

        // --- MCRF fixup pool: refs must be ONE contiguous u32 array, doodads first then wmos.
        // When only one side exists the split payload is aliased directly (zero copy). Doodad refs
        // (MCRD) are kept and their MDDF list stays index-aligned (count preserved above); WMO refs
        // (MCRW) are zeroed while WMOs are dropped (kAdtSplitSkipWmo), so the pool concat is normally
        // never needed on a split map (drop-WMO leaves the doodad side aliased directly).
        uint32_t poolBytes = 0;
        for (uint32_t i = 0; i < t.chunkCount; ++i)
        {
            ChunkFill& f = t.chunks[i];
            const uint32_t dr = skipDoodads ? 0u : f.mcrdSize;
            const uint32_t wr = skipWmo     ? 0u : f.mcrwSize;
            if (dr && wr) poolBytes += dr + wr;
        }
        t.mcrfPool.resize(poolBytes);
        uint32_t poolOff = 0;
        for (uint32_t i = 0; i < t.chunkCount; ++i)
        {
            ChunkFill& f = t.chunks[i];
            const uint32_t dr = skipDoodads ? 0u : f.mcrdSize;
            const uint32_t wr = skipWmo     ? 0u : f.mcrwSize;
            f.nDoodadRefs = dr / 4u;
            f.nMapObjRefs = wr / 4u;
            if (dr && wr)
            {
                uint8_t* dst = t.mcrfPool.data() + poolOff;
                std::memcpy(dst, f.mcrd, dr);
                std::memcpy(dst + dr, f.mcrw, wr);
                f.mcrf = dst;
                poolOff += dr + wr;
            }
            else if (dr) f.mcrf = f.mcrd;
            else if (wr) f.mcrf = f.mcrw;
            else         f.mcrf = nullptr;
        }
        g_statMcrfBytes.fetch_add(poolBytes, std::memory_order_relaxed);

        // --- per-chunk root header fixups (counts, holes, has_mcsh) ---
        for (uint32_t i = 0; i < t.chunkCount; ++i)
            FixChunkHeader(t, t.chunks[i]);

        // --- synthesize MCIN (absolute offsets into the root buffer, as stock PrepareChunk adds
        // them to area+0x80). Missing slots on a short root duplicate the last chunk so a stray
        // PrepareChunk never dereferences garbage.
        t.mcin.assign(8 + 256 * 16, 0);
        Wr32(t.mcin.data(), FourCC("MCIN"));
        Wr32(t.mcin.data() + 4, 256 * 16);
        for (uint32_t i = 0; i < 256; ++i)
        {
            const ChunkFill& f = t.chunks[i < t.chunkCount ? i : t.chunkCount - 1];
            uint8_t* e = t.mcin.data() + 8 + i * 16;
            Wr32(e + 0, static_cast<uint32_t>(f.rootMcnkHdr - rb)); // absolute file offset
            Wr32(e + 4, Rd32(f.rootMcnkHdr + 4) + 8);               // size (unread; hygiene)
            // flags(+8) = 0 (not built), asyncId(+12) = 0
        }

        // --- synthesize empty top-level chunks for whatever the split trio did not provide;
        // CMapArea::Create dereferences these eight unconditionally.
        t.synthEmpty.reserve(8 * 8);
        auto synth = [&](uint32_t tag) -> uint8_t* {
            const size_t at = t.synthEmpty.size();
            t.synthEmpty.resize(at + 8, 0);
            Wr32(t.synthEmpty.data() + at, tag);
            return t.synthEmpty.data() + at;
        };
        if (!mtex) mtex = synth(FourCC("MTEX"));
        if (!mmdx) mmdx = synth(FourCC("MMDX"));
        if (!mmid) mmid = synth(FourCC("MMID"));
        if (!mwmo) mwmo = synth(FourCC("MWMO"));
        if (!mwid) mwid = synth(FourCC("MWID"));
        if (!mddf) mddf = synth(FourCC("MDDF"));
        if (!modf) modf = synth(FourCC("MODF"));

        // --- patch the root MHDR so the stock parser finds everything. Offsets are stored as
        // (chunkHeader - mhdr); 32-bit wraparound makes cross-buffer deltas exact.
        auto delta = [&](const uint8_t* hdr) -> uint32_t {
            return hdr ? static_cast<uint32_t>(hdr - mhdr) : 0u;
        };
        Wr32(mhdr + 0x04, delta(t.mcin.data()));
        Wr32(mhdr + 0x08, delta(mtex));
        Wr32(mhdr + 0x0C, delta(mmdx));
        Wr32(mhdr + 0x10, delta(mmid));
        Wr32(mhdr + 0x14, delta(mwmo));
        Wr32(mhdr + 0x18, delta(mwid));
        Wr32(mhdr + 0x1C, delta(mddf));
        Wr32(mhdr + 0x20, delta(modf));
        Wr32(mhdr + 0x24, delta(mfbo));
        Wr32(mhdr + 0x28, delta(mh2o)); // gated by non-zero offset + chunk size, both true iff found
        Wr32(mhdr + 0x2C, delta(mtxf));
        uint32_t mhdrFlags = Rd32(mhdr);
        mhdrFlags = mfbo ? (mhdrFlags | 0x1u) : (mhdrFlags & ~0x1u);
        Wr32(mhdr, mhdrFlags);
        // 3.3.5 derives the alpha texture dim as 0x40 >> byte(MHDR+0x30); Cata parks its MAMP
        // amplitude there. Park the value and zero the byte so the dim stays the stock 64.
        if (mhdrSize > 0x30)
        {
            if (mhdr[0x30]) t.mampValue = mhdr[0x30];
            mhdr[0x30] = 0;
        }
        return true;
    }

    /// SEH shell around the parser: a fault on malformed data becomes a logged failure, never a
    /// crash. No unwindable locals here (C2712).
    bool ParseAndPatchGuarded(SplitTile* t)
    {
        __try
        {
            return ParseAndPatch(*t);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // ---------------------------------------------------------------- the async chain
    void __cdecl SplitStageComplete(void* areaCtx); // fwd

    /**
     * @brief Arms one async whole-file read for a stage: open -> size -> AllocRawAreaData ->
     *        CAsyncObject{file,buf,size,ctx=area,cb} -> area+0x70/+0x6C -> enqueue.
     *
     * Mirrors the native CMapArea::Load body field-for-field so the stock purge guard/cancel path
     * treats the read exactly like the stock single read. Returns false (nothing armed, area fields
     * untouched) when the stage's file is absent or empty.
     */
    bool StartStage(SplitTile& t, Stage stage)
    {
        const char* name = (stage == Stage::Tex) ? t.texName : t.objName;
        void* file = OpenFile(name);
        if (!file) return false;
        const uint32_t size = SizeOfFile(file);
        if (size == 0 || size == 0xFFFFFFFFu) { CloseFile(file); return false; }
        uint8_t* buf = AllocRaw(size);
        if (!buf) { CloseFile(file); return false; }
        void* async = AsyncAlloc();
        if (!async) { FreeRaw(buf, size); CloseFile(file); return false; }

        if (stage == Stage::Tex) { t.texBuf = buf; t.texSize = size; t.texOwned = true; }
        else                     { t.objBuf = buf; t.objSize = size; t.objOwned = true; }

        At<void*>(async, wld::kOffAsyncFile)     = file;
        At<void*>(async, wld::kOffAsyncBuffer)   = buf;
        At<uint32_t>(async, wld::kOffAsyncSize)  = size;
        At<void*>(async, wld::kOffAsyncCtx)      = t.area;
        At<void*>(async, wld::kOffAsyncCallback) = reinterpret_cast<void*>(&SplitStageComplete);

        t.stage    = stage;
        t.curAsync = async;
        At<void*>(t.area, adt::kOffTileAsyncRead)  = async;
        At<void*>(t.area, adt::kOffTileFileHandle) = file;
        AsyncEnqueue(async);
        return true;
    }

    /**
     * @brief Final step, on the main thread in the exact slot the stock parser would run: parse +
     *        patch, then hand the area to the UNCHANGED stock CMapArea::Create, then replicate the
     *        native completion epilogue (async retire, zero +0x70/+0x6C).
     */
    void FinalizeTile(SplitTile& t)
    {
        void* area = t.area;
        const bool parsed = ParseAndPatchGuarded(&t);
        if (!parsed)
        {
            g_statFailures.fetch_add(1, std::memory_order_relaxed);
            WLOG_ERROR("adt-split: tile %d_%d parse FAILED; running the stock parser over the raw root "
                       "(native corrupt-data behaviour)", t.tileFirst, t.tileSecond);
        }
        t.complete = parsed; // gate the ProcessIffChunks direct-fill on a good index only
        wxl::game::Native<adt::TileAreaCreateFn>(adt::kTileAreaCreate)(area, nullptr);

        if (t.curAsync) { AsyncRetire(t.curAsync); t.curAsync = nullptr; } // closes the last file
        At<void*>(area, adt::kOffTileAsyncRead)  = nullptr;
        At<void*>(area, adt::kOffTileFileHandle) = nullptr;
        t.stage = Stage::Done;

        if (parsed)
        {
            g_statTilesLoaded.fetch_add(1, std::memory_order_relaxed);
            if (t.mtxp) g_statMtxpTiles.fetch_add(1, std::memory_order_relaxed);
            g_statMclvChunks.fetch_add(t.mclvChunks, std::memory_order_relaxed);
            g_statHoleChunks.fetch_add(t.hiResHoleChunks, std::memory_order_relaxed);
            WLOG_INFO("adt-split: tile %d_%d loaded (root %u B, tex %u B, obj %u B, %u chunks, mcrf %zu B)",
                      t.tileFirst, t.tileSecond, t.rootSize, t.texSize, t.objSize, t.chunkCount,
                      t.mcrfPool.size());
            ev::AdtSplitTileLoadArgs a{ t.tileFirst, t.tileSecond, t.rootSize, t.texSize, t.objSize,
                                        t.chunkCount };
            ev::Emit(ev::Event::OnAdtSplitTileLoad, &a);
        }
    }

    /**
     * @brief Main-thread completion for every stage of the 3-read chain (installed as async+0x10,
     *        invoked by the stock drain as callback(ctx=area)).
     *
     * Advances root -> _tex0 -> _obj0 -> finalize. A missing _tex0/_obj0 skips forward (the tile
     * then renders with whatever the trio provided). A tile torn down mid-chain has its async
     * cancelled by the stock purge and its registry entry removed by the destructor detour, so a
     * stale invocation finds no record and touches nothing.
     */
    void __cdecl SplitStageComplete(void* areaCtx)
    {
        SplitTile* t = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            t = FindTileLocked(areaCtx);
        }
        if (!t) return;

        switch (t->stage)
        {
        case Stage::Root:
            if (t->curAsync) { AsyncRetire(t->curAsync); t->curAsync = nullptr; } // closes root file
            At<void*>(t->area, adt::kOffTileAsyncRead)  = nullptr;
            At<void*>(t->area, adt::kOffTileFileHandle) = nullptr;
            if (!StartStage(*t, Stage::Tex) && !StartStage(*t, Stage::Obj))
                FinalizeTile(*t);
            break;
        case Stage::Tex:
            if (t->curAsync) { AsyncRetire(t->curAsync); t->curAsync = nullptr; } // closes tex file
            At<void*>(t->area, adt::kOffTileAsyncRead)  = nullptr;
            At<void*>(t->area, adt::kOffTileFileHandle) = nullptr;
            if (!StartStage(*t, Stage::Obj))
                FinalizeTile(*t);
            break;
        case Stage::Obj:
            // native ordering: the parser runs BEFORE the async retire (+0x70 stays armed under it)
            FinalizeTile(*t);
            break;
        default:
            break;
        }
    }

    // ---------------------------------------------------------------- Cata+ WDL direct read
    // A Cata+ .wdl puts MLDD/MLDX/MLDF/MLMD/MLMX (low-detail doodad/wmo placements) between MVER
    // and the chunks 3.3.5 knows, and inserts MAOE between each tile's MARE and MAHO. The native
    // CMap::LoadWdl (0x007CC310) only tests for MWMO after MVER; on a miss it treats whatever sits
    // there as the MAOF offset table, so an MLDD chunk's bytes become "file offsets", the 64x64
    // grid loop derefs garbage and the client #132s at ~0x007CC41F.
    //
    // Same philosophy as the split-ADT direct-fill: READ the real format and fill the stock WDL
    // runtime (kWdlState) with the true data. The reader below replays the native body faithfully
    // -- same open/alloc/read prologue (CMap::AllocRawAreaData, so the native unload's SMemFree
    // pairs), a top-level walk that skips the ML* family to find the REAL MAOF, and a
    // byte-faithful replay of the native grid loop (CMap::AllocAreaLow per non-zero offset, the
    // 0x6D-iteration 545-short MARE min/max scan, the exact bounds/center/radius float math, and
    // the MAHO hole-mask budget count) with MAOE skipped between MARE and MAHO. Distant terrain
    // renders from the REAL MARE heights.
    //
    // TODO(adt-split/wdl-wmo): distant WMOs. A Cata+ WDL carries them as MLDD/MLDX (doodads) and
    // MLMD/MLMX (wmos, MODF-like 0x28-byte entries without bounds) instead of 3.3.5's
    // MWMO/MWID/MODF. wdlState[1..4] stay 0 for now; lighting them up means synthesizing
    // MWMO/MWID/MODF images from MLMD (+ a name table) and running the native mapobjdef tail.

    enum class WdlKind { Missing, Stock, CataPlus };

    /// Bounds-checked sniff of the first bytes of "<mapPath>\<mapName>.wdl": reads MVER and the tag
    /// of the chunk after it into a stack buffer. "ML.." there = Cata+ family. Malformed or absent
    /// input never faults (fixed-size local reads only) and classifies as Stock/Missing so the
    /// native path keeps its behaviour. No unwindable locals (SEH shell wraps it, C2712).
    WdlKind SniffWdlKind(const char* mapPath, const char* mapName)
    {
        char path[0x120];
        std::snprintf(path, sizeof path, "%s\\%s.wdl", mapPath, mapName);
        void* h = OpenFile(path);
        if (!h) return WdlKind::Missing;
        uint8_t head[0x20] = { 0 };
        const bool ok = ReadBytes(h, head, sizeof head);
        CloseFile(h);
        if (!ok || Rd32(head) != FourCC("MVER")) return WdlKind::Stock;
        const uint32_t mverSize = Rd32(head + 4);
        if (mverSize > sizeof head - 12) return WdlKind::Stock; // tag after MVER not in the window
        const uint32_t tag = Rd32(head + 8 + mverSize);
        return (tag >> 16) == ((uint32_t('M') << 8) | uint32_t('L')) ? WdlKind::CataPlus
                                                                     : WdlKind::Stock;
    }

    /// SEH shell: any fault while touching the real WDL bytes/SFile internals classifies as Stock
    /// (native behaviour unchanged), never a crash.
    WdlKind SniffWdlKindGuarded(const char* mapPath, const char* mapName)
    {
        __try
        {
            return SniffWdlKind(mapPath, mapName);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return WdlKind::Stock;
        }
    }

    /// One CMapAreaLow slot fill, byte-faithful to the native grid-loop body (decompile lines
    /// ~808711-808781): obj+0x44 = MARE data, 545-short min/max scan, the exact bounds/center/
    /// radius float math (same literals the native constants round to), and the MAHO budget count
    /// (0xC per un-holed cell). `mahoData` may be null (no holes -> full 0xC00 budget, like stock).
    void FillAreaLow(void* obj, uint32_t col, uint32_t row, uint8_t* mareData, const uint8_t* mahoData)
    {
        At<uint8_t*>(obj, 0x44) = mareData;

        // native: psVar9 = (short*)(obj+0x44) + 2, reads [-2..+2] over 0x6D iterations = 545 s16
        const int16_t* hp = reinterpret_cast<const int16_t*>(mareData);
        int32_t mx = -100000, mn = 100000; // native seeds 0xFFFE7960 / 100000
        for (uint32_t i = 0; i < 545; ++i)
        {
            const int32_t v = hp[i];
            if (v > mx) mx = v;
            if (v < mn) mn = v;
        }

        At<int32_t>(obj, 0x38) = static_cast<int32_t>(col);
        At<int32_t>(obj, 0x3C) = static_cast<int32_t>(row);
        // native literals: 33.333332 (= tileSize/16), 17066.666 (grid origin), 533.3333 (tile size)
        const float x = -(static_cast<float>(row * 16u) * 33.333332f) + 17066.666f;
        const float y = -(static_cast<float>(col * 16u) * 33.333332f) + 17066.666f;
        At<float>(obj, 0x2C) = x;
        At<float>(obj, 0x10) = x;
        At<float>(obj, 0x30) = y;
        At<float>(obj, 0x14) = y;
        At<float>(obj, 0x18) = static_cast<float>(mx);
        At<float>(obj, 0x04) = x - 533.3333f;
        At<float>(obj, 0x08) = y - 533.3333f;
        At<float>(obj, 0x0C) = static_cast<float>(mn);
        At<float>(obj, 0x1C) = (At<float>(obj, 0x10) + At<float>(obj, 0x04)) * 0.5f;
        At<float>(obj, 0x20) = (At<float>(obj, 0x08) + At<float>(obj, 0x14)) * 0.5f;
        At<float>(obj, 0x24) = (At<float>(obj, 0x0C) + At<float>(obj, 0x18)) * 0.5f;
        const float dx = At<float>(obj, 0x10) - At<float>(obj, 0x1C);
        const float dy = At<float>(obj, 0x14) - At<float>(obj, 0x20);
        const float dz = At<float>(obj, 0x18) - At<float>(obj, 0x24);
        At<float>(obj, 0x28) = std::sqrt(dz * dz + dy * dy + dx * dx);

        if (mahoData)
        {
            At<const uint8_t*>(obj, 0x48) = mahoData;
            uint32_t budget = 0;
            const uint16_t* masks = reinterpret_cast<const uint16_t*>(mahoData);
            for (uint32_t m = 0; m < 16; ++m)
                for (uint32_t bit = 0; bit < 16; ++bit)
                    if ((masks[m] & (1u << bit)) == 0)
                        budget += 0xC;
            At<uint32_t>(obj, 0x40) = budget;
        }
        else
        {
            At<uint32_t>(obj, 0x48) = 0;
            At<uint32_t>(obj, 0x40) = 0xC00; // 16x16 cells x 0xC, no holes
        }
    }

    /**
     * @brief Reads a Cata+ WDL directly into the stock WDL runtime (wdlState), replaying the
     *        native CMap::LoadWdl body over the real layout.
     *
     * Prologue like native: open through the storage seam, size, CMap::AllocRawAreaData (so the
     * native unload's SMemFree pairs), whole-file read, buffer -> wdlState[0]. Then a
     * bounds-checked top-level walk skips the ML* placement family (MLDD/MLDX/MLDF/MLMD/MLMX) and
     * points wdlState[5] at the REAL MAOF data; wdlState[1..4] (MWMO/MWID/MODF) stay 0 -- see the
     * distant-WMO TODO above. The grid loop then replays the native per-tile body for every
     * non-zero MAOF offset (CMap::AllocAreaLow -> slot, MARE heights, bounds, MAHO), skipping the
     * Cata+ MAOE chunk sitting between MARE and MAHO. The slot array [6..0x1005] is only written
     * at non-zero offsets, exactly like stock (it is guaranteed clean on entry: static ctor
     * 0x007CC2C0 at startup, CMap::UnloadWdl 0x007CC770 on every CMap__Purge, which CMap__Load
     * runs before LoadWdl).
     *
     * @return 1 like the native success path; 0 with wdlState[0]/[5] = 0 (the native missing-file
     *         end-state) when the file cannot be fetched or has no usable MAOF.
     */
    uint32_t ReadCataWdl(int* wdlState, const char* mapPath, const char* mapName)
    {
        char path[0x120];
        std::snprintf(path, sizeof path, "%s\\%s.wdl", mapPath, mapName);
        void* h = OpenFile(path);
        if (!h) return 0;
        const uint32_t size = SizeOfFile(h);
        if (size == 0 || size == 0xFFFFFFFFu) { CloseFile(h); return 0; }
        uint8_t* buf = AllocRaw(size);
        if (!buf) { CloseFile(h); return 0; }
        const bool ok = ReadBytes(h, buf, size);
        CloseFile(h);
        if (!ok) { FreeRaw(buf, size); return 0; }

        // --- top-level walk: skip MVER + ML*, find the real MAOF ---
        uint8_t* maofData = nullptr;
        uint32_t maofSize = 0, mlChunks = 0;
        WalkTop(buf, size, [&](uint32_t tag, uint8_t* hdr, uint32_t sz) {
            if (tag == FourCC("MAOF")) { maofData = hdr + 8; maofSize = sz; }
            else if ((tag >> 16) == ((uint32_t('M') << 8) | uint32_t('L'))) ++mlChunks;
        });
        if (!maofData || maofSize < adt::kWdlSlotCount * 4)
        {
            FreeRaw(buf, size);
            WLOG_ERROR("adt-split: Cata+ WDL '%s' has no usable MAOF (size %u); no distant terrain",
                       path, maofSize);
            return 0;
        }
        wdlState[0] = reinterpret_cast<int>(buf);
        wdlState[5] = reinterpret_cast<int>(maofData);
        // wdlState[1..4] = MWMO/MWID/MODF: left at their clean zeroes (distant-WMO TODO above)

        // --- the native grid loop over the 64x64 MAOF offsets, bounds-checked ---
        uint32_t tiles = 0, holed = 0, badSlots = 0;
        for (uint32_t row = 0; row < 64; ++row)
        {
            for (uint32_t col = 0; col < 64; ++col)
            {
                const uint32_t idx = row * 64u + col;
                const uint32_t off = Rd32(maofData + idx * 4);
                if (off == 0) continue;
                // MARE chunk at the absolute file offset (native adds it to the buffer base blind;
                // we verify tag + that the 545-short heightmap is inside the file)
                if (off + 8 > size || size - (off + 8) < 545u * 2u ||
                    Rd32(buf + off) != FourCC("MARE"))
                {
                    ++badSlots;
                    continue;
                }
                uint8_t* mareHdr  = buf + off;
                uint8_t* mareData = mareHdr + 8;
                const uint32_t mareSize = Rd32(mareHdr + 4);

                // chunk after MARE: 3.3.5 puts MAHO right there; Cata+ inserts MAOE first. Walk a
                // few headers, skipping foreigners, until MAHO / the next tile / the buffer end.
                const uint8_t* mahoData = nullptr;
                if (mareSize <= size - (off + 8))
                {
                    uint32_t nextOff = off + 8 + mareSize;
                    for (int hop = 0; hop < 3 && nextOff + 8 <= size; ++hop)
                    {
                        const uint32_t tag = Rd32(buf + nextOff);
                        const uint32_t sz  = Rd32(buf + nextOff + 4);
                        if (sz > size - nextOff - 8) break;
                        if (tag == FourCC("MAHO"))
                        {
                            if (sz >= 16u * 2u) mahoData = buf + nextOff + 8;
                            break;
                        }
                        if (tag == FourCC("MARE")) break; // next tile, no MAHO for this one
                        nextOff += 8 + sz;                // skip MAOE (or other foreigners)
                    }
                }

                void* obj = wxl::game::Native<adt::AllocAreaLowFn>(adt::kAllocAreaLow)();
                if (!obj) { ++badSlots; continue; }
                wdlState[6 + idx] = reinterpret_cast<int>(obj);
                FillAreaLow(obj, col, row, mareData, mahoData);
                ++tiles;
                if (mahoData) ++holed;
            }
        }
        // native tail (MWMO/MODF mapobjdef creation) is a no-op with wdlState[4] == 0

        if (badSlots)
            WLOG_WARN("adt-split: Cata+ WDL '%s': %u MAOF slot(s) skipped (out of bounds or not MARE)",
                      path, badSlots);
        WLOG_INFO("adt-split: read Cata+ WDL for '%s\\%s' (%u low-detail tiles, %u with holes, "
                  "%u ML* chunks skipped; distant WMOs TODO)",
                  mapPath, mapName, tiles, holed, mlChunks);
        return 1;
    }

    /// SEH shell around the Cata+ WDL reader: a fault on malformed data becomes a logged failure,
    /// never a crash. State stays engine-legal either way: every filled slot is a valid
    /// CMapAreaLow and wdlState[0] (when set) is unload-owned. No unwindable locals here (C2712).
    uint32_t ReadCataWdlGuarded(int* wdlState, const char* mapPath, const char* mapName)
    {
        __try
        {
            return ReadCataWdl(wdlState, mapPath, mapName);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    /**
     * @brief Detours CMap::LoadWdl 0x007CC310 (once per map load, from CMap__Load).
     *
     * Fast path first: a map already cached as NON-split runs the untouched original (one hash
     * lookup). Otherwise the WDL header is sniffed (the g_splitMaps cache cannot answer here: the
     * WDL loads BEFORE any tile is requested, so on a fresh map the _tex0 probe has not run yet --
     * the WDL's own after-MVER tag is the authoritative trigger). A Cata+ WDL is read directly
     * into the stock WDL runtime; everything else (stock WDL, missing file, malformed header) runs
     * the byte-identical native body. A Cata+ WDL NEVER falls through to the native parser -- that
     * is the #132 at ~0x007CC41F this detour exists to prevent.
     */
    uint32_t __fastcall hkLoadWdl(int* wdlState, void* edx, const char* mapPath, const char* mapName)
    {
        if (wdlState && mapPath && mapName)
        {
            bool knownMonolithic = false;
            {
                std::string key(mapPath);
                key += '\\';
                key += mapName;
                std::lock_guard<std::mutex> lock(g_mutex);
                auto it = g_splitMaps.find(key);
                knownMonolithic = it != g_splitMaps.end() && !it->second;
            }
            if (!knownMonolithic && SniffWdlKindGuarded(mapPath, mapName) == WdlKind::CataPlus)
            {
                const uint32_t result = ReadCataWdlGuarded(wdlState, mapPath, mapName);
                if (result)
                    g_statWdlRead.fetch_add(1, std::memory_order_relaxed);
                else
                {
                    g_statFailures.fetch_add(1, std::memory_order_relaxed);
                    WLOG_ERROR("adt-split: Cata+ WDL read failed for '%s\\%s'; continuing without "
                               "distant terrain", mapPath, mapName);
                }
                return result;
            }
        }
        return g_origLoadWdl(wdlState, edx, mapPath, mapName);
    }

    // ---------------------------------------------------------------- detours
    /**
     * @brief Detours CMapArea::Load 0x007D7150 (the per-tile open + async-read arm).
     *
     * Split tile: arms the 3-read chain (root first, stock-shaped) and returns without the original.
     * Non-split tile, unknown name, or any arming failure: the untouched native body runs -- the
     * stock path stays byte-identical for classic maps.
     */
    void __fastcall hkTileAreaLoad(void* area, void* edx, const char* filename)
    {
        if (filename && area)
        {
            std::string key;
            if (IsSplitTileName(filename, key))
            {
                // stage 1 (root), shaped exactly like the native body
                void* file = OpenFile(filename);
                if (file)
                {
                    const uint32_t size = SizeOfFile(file);
                    uint8_t* buf   = (size && size != 0xFFFFFFFFu) ? AllocRaw(size) : nullptr;
                    void*    async = buf ? AsyncAlloc() : nullptr;
                    if (async)
                    {
                        auto tile = std::make_unique<SplitTile>();
                        tile->area       = area;
                        tile->tileFirst  = At<int>(area, adt::kOffTileIdxFirst);
                        tile->tileSecond = At<int>(area, adt::kOffTileIdxSecond);
                        const size_t base = std::strlen(filename) - 4;
                        std::snprintf(tile->texName, sizeof tile->texName, "%.*s_tex0.adt",
                                      static_cast<int>(base), filename);
                        std::snprintf(tile->objName, sizeof tile->objName, "%.*s_obj0.adt",
                                      static_cast<int>(base), filename);
                        tile->rootBuf  = buf;
                        tile->rootSize = size;
                        tile->stage    = Stage::Root;
                        tile->curAsync = async;

                        At<void*>(async, wld::kOffAsyncFile)     = file;
                        At<void*>(async, wld::kOffAsyncBuffer)   = buf;
                        At<uint32_t>(async, wld::kOffAsyncSize)  = size;
                        At<void*>(async, wld::kOffAsyncCtx)      = area;
                        At<void*>(async, wld::kOffAsyncCallback) = reinterpret_cast<void*>(&SplitStageComplete);

                        At<void*>(area, adt::kOffTileFileHandle)   = file;
                        At<uint32_t>(area, adt::kOffTileFileSize)  = size;
                        At<void*>(area, adt::kOffTileFileBuffer)   = buf;
                        At<void*>(area, adt::kOffTileAsyncRead)    = async;
                        {
                            std::lock_guard<std::mutex> lock(g_mutex);
                            g_tiles[area] = std::move(tile);
                        }
                        g_statTilesResident.fetch_add(1, std::memory_order_relaxed);
                        AsyncEnqueue(async);
                        return;
                    }
                    if (buf) FreeRaw(buf, size);
                    CloseFile(file);
                }
                g_statFailures.fetch_add(1, std::memory_order_relaxed);
                WLOG_WARN("adt-split: split arming failed for '%s', falling back to the native load", filename);
            }
        }
        g_origTileAreaLoad(area, edx, filename);
    }

    /**
     * @brief Detours CMapChunk::ProcessIffChunks 0x007C3A10 (the sequential sub-chunk walk).
     *
     * For a chunk of a completed split tile the walk is REPLACED by direct pointer assignment: the
     * chunk+0x11C..+0x13C sub-chunk slots aim straight into the three resident split buffers (and
     * the tiny MCRF pool), zero payload copies. Non-split chunks run the untouched original. The
     * firstBuild in-place size patches are not needed on the split path (no stock walk ever reads
     * those size fields again).
     */
    void __fastcall hkProcessIffChunks(void* chunk, void* edx, int firstBuild)
    {
        // Fast path: with no split tile resident (every classic 3.3.5 map, always) the stock walk runs
        // with only a single relaxed atomic load added -- no owner-link deref, no lock, no hash lookup.
        // Keeps the stock chunk-build path effectively free; the split work is gated to split maps only.
        if (g_statTilesResident.load(std::memory_order_relaxed) == 0)
        {
            g_origProcessIff(chunk, edx, firstBuild);
            return;
        }

        // chunk -> owning area, the engine's own link arithmetic: *((link & ~1) + 8)
        void* area = nullptr;
        const uint32_t link = At<uint32_t>(chunk, adt::kOffChunkTexOwnerSrc);
        if (link != 0 && (link & 1u) == 0)
            area = *reinterpret_cast<void**>(static_cast<uintptr_t>(link) + 8);

        const ChunkFill* f = nullptr;
        if (area)
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            SplitTile* t = FindTileLocked(area);
            if (t && t->complete)
            {
                const int lx = At<int>(chunk, adt::kOffMapChunkIndexX);
                const int ly = At<int>(chunk, adt::kOffMapChunkIndexY);
                const uint32_t idx = static_cast<uint32_t>(ly) * 16u + static_cast<uint32_t>(lx);
                uint8_t* raw = At<uint8_t*>(chunk, adt::kOffChunkRawMcnk);
                if (idx < 256 && t->chunks[idx].rootMcnkHdr == raw)
                    f = &t->chunks[idx];
                else
                    WLOG_WARN("adt-split: chunk (%d,%d) raw MCNK mismatch, using stock walk", lx, ly);
            }
        }
        if (!f)
        {
            g_origProcessIff(chunk, edx, firstBuild);
            return;
        }

        (void)firstBuild;
        uint8_t* raw = At<uint8_t*>(chunk, adt::kOffChunkRawMcnk);
        At<void*>(chunk, adt::kOffChunkMcnkHeader) = raw + 8;                 // 0x80-byte SMChunk header
        At<void*>(chunk, adt::kOffChunkMcvt) = f->mcvt;
        At<void*>(chunk, adt::kOffChunkMccv) = f->mccv;
        At<void*>(chunk, adt::kOffChunkMcnr) = f->mcnr;
        At<void*>(chunk, adt::kOffChunkMcsh) = f->mcsh;
        At<void*>(chunk, adt::kOffChunkMcly) = f->mcly;
        At<void*>(chunk, adt::kOffChunkMcal) = f->mcal;
        At<void*>(chunk, adt::kOffChunkMcrf) = f->mcrf;
        At<void*>(chunk, adt::kOffChunkMclq) = (f->mclq && f->mclqSize) ? f->mclq : nullptr;
        At<void*>(chunk, adt::kOffChunkMcse) = (f->mcse && f->mcseSize >= 0x1C) ? f->mcse : nullptr;
        g_statChunksFilled.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Detours the CMapArea destructor 0x007D6E10 (tile teardown, frees area+0x80).
     *
     * Releases the tile's side record AFTER the original body -- by then every chunk of the tile is
     * already purged (the purge path runs CMapArea::PurgeXXX before the destructor), so no CMapChunk
     * pointer into the _tex0/_obj0 buffers can outlive them. Handles the purge-cancel ownership
     * transfer: a DEFERRED cancel (read in service) hands the in-flight stage's buffer to the
     * engine's deferred cleanup (which frees async+0x04) and zeroes area+0x80 -- in that case the
     * transferred buffer is NOT freed here, and the orphaned root buffer (whose +0x80 slot was
     * zeroed even though the in-flight buffer was a later stage's) IS.
     */
    void __fastcall hkTileAreaDestroy(void* area)
    {
        std::unique_ptr<SplitTile> t;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_tiles.find(area);
            if (it != g_tiles.end())
            {
                t = std::move(it->second);
                g_tiles.erase(it);
            }
        }

        bool freeRootAfter = false;
        if (t)
        {
            if (!t->complete)
            {
                const bool deferredCancel =
                    At<void*>(area, adt::kOffTileFileBuffer) == nullptr && t->rootBuf != nullptr;
                if (deferredCancel)
                {
                    if (t->stage == Stage::Tex)      t->texOwned = false; // deferred cleanup frees it
                    else if (t->stage == Stage::Obj) t->objOwned = false;
                    if (t->stage != Stage::Root)     freeRootAfter = true; // +0x80 zeroed, root orphaned
                }
            }
            // Defensive: a teardown path that skipped the purge cancel leaves our async armed; retire
            // it (spin-waits an in-service read, unlinks the completion, closes the file) first.
            void* pending = At<void*>(area, adt::kOffTileAsyncRead);
            if (pending && pending == t->curAsync)
            {
                AsyncRetire(pending);
                t->curAsync = nullptr;
                At<void*>(area, adt::kOffTileAsyncRead)  = nullptr;
                At<void*>(area, adt::kOffTileFileHandle) = nullptr;
            }
        }

        g_origTileAreaDestroy(area);

        if (t)
        {
            // Height-blend "_h" handles are module-owned (the stock destructor only frees the tile's
            // own area+0x60 diffuse slots); release each once, after every chunk of the tile purged.
            for (const SplitTile::HeightSlot& hs : t->heightSlots)
                if (hs.owned && hs.tex)
                    wxl::game::Native<adt::TextureReleaseFn>(adt::kTextureRelease)(hs.tex);
            if (t->texOwned && t->texBuf) FreeRaw(t->texBuf, t->texSize);
            if (t->objOwned && t->objBuf) FreeRaw(t->objBuf, t->objSize);
            if (freeRootAfter)            FreeRaw(t->rootBuf, t->rootSize);
            g_statTilesResident.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    // ---------------------------------------------------------------- install
    /**
     * @brief Installs the four split-ADT detours (load seam, per-chunk fill seam, teardown seam,
     *        Cata+ WDL guard).
     */
    // ---------------------------------------------------------------- split-tile texture manager
    /**
     * @brief Detours CMapArea::LoadTextures (0x007D6D20) so a split tile's tex-owner array is built
     *        from the real MDID FileDataIDs instead of the (absent) MTEX.
     *
     * Each MDID id is resolved through TextureFilePath.db2 to a client path; the paths are packed
     * NUL-terminated into the tile's persistent texNameBlob (MDID order = MCLY.textureId order) and
     * that blob is handed to the UNCHANGED stock builder. The builder sizes area+0x60 to the name
     * count and points each slot's name at the blob (resident for the tile lifetime). Nothing here
     * touches the ADT's MTEX -- we adapt TLK to read the ADT, per the project rule.
     */
    void __fastcall hkAreaLoadTextures(void* area, void* edx, const void* mtexData, uint32_t mtexSize)
    {
        if (SplitTile* t = FindTileBrief(area); t && !t->mdid.empty())
        {
            if (t->texNameBlob.empty())
            {
                for (uint32_t fdid : t->mdid)
                {
                    const char* p = wxl::fdid::ResolveTexture(fdid);
                    const char* s = p ? p : ""; // unresolved -> empty name -> client renders green
                    t->texNameBlob.insert(t->texNameBlob.end(), s, s + std::strlen(s) + 1);
                }
            }
            g_origAreaLoadTextures(area, edx, t->texNameBlob.data(),
                                    static_cast<uint32_t>(t->texNameBlob.size()));
            return;
        }
        g_origAreaLoadTextures(area, edx, mtexData, mtexSize);
    }

    /**
     * @brief Detours CMap::LoadTerrainTexture (0x007D6980) -- the per-slot loader (slot[1]=Load(slot[0])).
     *
     * For a split tile the slot name is already a fully-resolved client path (set above), so it is
     * opened as-is via CMap::LoadTexture, bypassing the stock "_s.blp" suffix rewrite that assumes
     * the legacy base/diffuse+specular naming. An empty (unresolved) name yields a null handle, which
     * the client draws as the green missing-texture placeholder -- never fatal.
     */
    void __fastcall hkLoadTerrainTexture(void* area, void* edx, void** slot, uint32_t index)
    {
        if (SplitTile* t = FindTileBrief(area); t && !t->mdid.empty())
        {
            const char* name = static_cast<const char*>(slot[0]);
            slot[1] = (name && name[0])
                            ? wxl::game::Native<adt::Map_LoadTextureFn>(adt::kMapLoadTexture)(name)
                            : nullptr;
            return;
        }
        g_origLoadTerrainTexture(area, edx, slot, index);
    }

    // ---------------------------------------------------------------- height-blend slot build
    /// i-th NUL-terminated string of a packed name blob, or null when out of range/unterminated.
    const char* NthName(const char* blob, uint32_t size, uint32_t index)
    {
        const char* p   = blob;
        const char* end = blob + size;
        for (uint32_t i = 0; p < end; ++i)
        {
            const char* s = p;
            while (p < end && *p) ++p;
            if (p >= end) return nullptr; // unterminated tail
            if (i == index) return s;
            ++p;
        }
        return nullptr;
    }

    /**
     * @brief Lazily builds a tile's per-texture height slots (MTXP params + "_h" texture handles).
     *
     * Legion semantics (FUN_00ca1272 / FUN_00c9c7b8): record i = MTXP[i] when present, else
     * {MTXF[i] flags, scale 0, offset 1}; flag 0x1 or default {0,1} params degrade to the solid
     * white height (weight = plain alpha). Otherwise the "_h" texture resolves by MHID FileDataID
     * (TextureFilePath.db2) or the "<diffuse>_h.blp" name, created through the by-name map texture
     * loader (content streams in asynchronously, like every terrain texture). Existence is probed
     * through the storage seam first so a missing file degrades to white instead of a dead handle.
     * Main thread only (same thread class as tile load/teardown and the terrain draw).
     */
    void BuildHeightSlots(SplitTile& t)
    {
        t.heightBuilt = true;
        uint32_t count = t.mtxpSize / 16u;
        if (t.mdid.size() > count) count = static_cast<uint32_t>(t.mdid.size());
        if (t.mhid.size() > count) count = static_cast<uint32_t>(t.mhid.size());
        if (count == 0 || count > 512) return; // sanity cap
        t.heightSlots.resize(count);

        uint32_t loaded = 0, white = 0;
        for (uint32_t i = 0; i < count; ++i)
        {
            SplitTile::HeightSlot& hs = t.heightSlots[i];
            uint32_t flags = 0;
            if (t.mtxp && (i + 1) * 16u <= t.mtxpSize)
            {
                flags = Rd32(t.mtxp + i * 16u);
                std::memcpy(&hs.scale, t.mtxp + i * 16u + 4, 4);
                std::memcpy(&hs.offset, t.mtxp + i * 16u + 8, 4);
            }
            else
            {
                if (t.mtxfData && (i + 1) * 4u <= t.mtxfSize) flags = Rd32(t.mtxfData + i * 4u);
                hs.scale = 0.0f; hs.offset = 1.0f;
            }
            hs.exp = static_cast<uint8_t>((flags >> 4) & 0xFu); // UV-tiling exponent (Legion FUN_00c9e1ef)
            if ((flags & 0x1u) != 0 ||                       // "no _s/_h variants exist"
                (hs.scale == 0.0f && hs.offset == 1.0f))     // default params -> white (Legion-exact)
            {
                ++white;
                continue;
            }

            // Resolve the "_h" file: FileDataID first (Legion 8.1+ MHID), then the name convention.
            char nameBuf[300];
            const char* path = nullptr;
            if (i < t.mhid.size() && t.mhid[i])
                path = wxl::fdid::ResolveTexture(t.mhid[i]);
            if (!path)
            {
                const char* dif = nullptr;
                if (!t.texNameBlob.empty())
                    dif = NthName(t.texNameBlob.data(), static_cast<uint32_t>(t.texNameBlob.size()), i);
                else if (t.mtexData)
                    dif = NthName(reinterpret_cast<const char*>(t.mtexData), t.mtexSizeParked, i);
                if (dif && dif[0])
                {
                    size_t len = std::strlen(dif);
                    if (len > 4 && _stricmp(dif + len - 4, ".blp") == 0) len -= 4;
                    if (len + 7 <= sizeof nameBuf)
                    {
                        std::memcpy(nameBuf, dif, len);
                        std::memcpy(nameBuf + len, "_h.blp", 7);
                        path = nameBuf;
                    }
                }
            }
            if (path && path[0])
            {
                if (void* probe = OpenFile(path)) // storage seam: host + archives both answer
                {
                    CloseFile(probe);
                    hs.tex   = wxl::game::Native<adt::Map_LoadTextureFn>(adt::kMapLoadTexture)(path);
                    hs.owned = hs.tex != nullptr;
                }
            }
            if (hs.tex) ++loaded; else ++white;
        }
        g_statHeightTex.fetch_add(loaded, std::memory_order_relaxed);
        WLOG_INFO("adt-split: tile %d_%d height slots built (%u textures, %u _h loaded, %u white)",
                  t.tileFirst, t.tileSecond, count, loaded, white);
    }

    bool InstallAdtSplit()
    {
        wxl::hook::Install("AdtSplit_TileAreaLoad", adt::kTileAreaLoad,
                           &hkTileAreaLoad, &g_origTileAreaLoad);
        wxl::hook::Install("AdtSplit_ProcessIffChunks", adt::kChunkProcessIffChunks,
                           &hkProcessIffChunks, &g_origProcessIff);
        wxl::hook::Install("AdtSplit_TileAreaDestroy", adt::kTileAreaDestroy,
                           &hkTileAreaDestroy, &g_origTileAreaDestroy);
        wxl::hook::Install("AdtSplit_LoadWdl", adt::kLoadWdl,
                           &hkLoadWdl, &g_origLoadWdl);
        wxl::hook::Install("AdtSplit_AreaLoadTextures", adt::kAreaLoadTextures,
                            &hkAreaLoadTextures, &g_origAreaLoadTextures);
        wxl::hook::Install("AdtSplit_LoadTerrainTexture", adt::kLazyLoadTexSlot,
                            &hkLoadTerrainTexture, &g_origLoadTerrainTexture);
        return true;
    }
}

// ---------------------------------------------------------------- public query surface
namespace wxl::runtime::adtsplit
{
    Stats GetStats()
    {
        Stats s{};
        s.splitMapsDetected = g_statSplitMaps.load(std::memory_order_relaxed);
        s.tilesLoaded       = g_statTilesLoaded.load(std::memory_order_relaxed);
        s.tilesResident     = g_statTilesResident.load(std::memory_order_relaxed);
        s.chunksFilled      = g_statChunksFilled.load(std::memory_order_relaxed);
        s.mcrfBytes         = g_statMcrfBytes.load(std::memory_order_relaxed);
        s.parkedMtxpTiles   = g_statMtxpTiles.load(std::memory_order_relaxed);
        s.parkedMclvChunks  = g_statMclvChunks.load(std::memory_order_relaxed);
        s.parkedHoleChunks  = g_statHoleChunks.load(std::memory_order_relaxed);
        s.loadFailures      = g_statFailures.load(std::memory_order_relaxed);
        s.wdlRead           = g_statWdlRead.load(std::memory_order_relaxed);
        s.heightTexLoaded   = g_statHeightTex.load(std::memory_order_relaxed);
        return s;
    }

    bool GetHeightLayer(void* area, uint32_t textureId, HeightLayer& out)
    {
        SplitTile* t = FindTileBrief(area);
        if (!t || !t->complete || !t->mtxp) return false; // no MTXP = nothing to blend by height
        if (!t->heightBuilt) BuildHeightSlots(*t);        // main thread, same class as load/teardown
        if (textureId < t->heightSlots.size())
        {
            const SplitTile::HeightSlot& hs = t->heightSlots[textureId];
            out.texture      = hs.tex;
            out.heightScale  = hs.scale;
            out.heightOffset = hs.offset;
            out.tilingExp    = hs.exp;
        }
        else
        {
            out.texture = nullptr; out.heightScale = 0.0f; out.heightOffset = 1.0f; out.tilingExp = 0;
        }
        return true;
    }

    uint32_t ResidentTilesRelaxed()
    {
        return g_statTilesResident.load(std::memory_order_relaxed);
    }

    int IsSplitMap()
    {
        // Compose the same "<dir>\<name>" prefix the tile-name keying uses from the live loader
        // globals (phasing swaps them per load, but the base map's key is what a user asks about).
        const char* dir  = reinterpret_cast<const char*>(wld::kMapDirStr);
        const char* name = reinterpret_cast<const char*>(wld::kMapNameStr);
        if (!dir[0] || !name[0]) return -1;
        std::string key(dir);
        key += '\\';
        key += name;
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_splitMaps.find(key);
        if (it == g_splitMaps.end()) return -1;
        return it->second ? 1 : 0;
    }

    static void FillStatus(const SplitTile& t, TileStatus& out)
    {
        out.tileFirst       = t.tileFirst;
        out.tileSecond      = t.tileSecond;
        out.rootSize        = t.rootSize;
        out.texSize         = t.texSize;
        out.objSize         = t.objSize;
        out.chunkCount      = t.chunkCount;
        out.complete        = t.complete;
        out.hasMtxp         = t.mtxp != nullptr;
        out.mclvChunks      = t.mclvChunks;
        out.hiResHoleChunks = t.hiResHoleChunks;
    }

    uint32_t ResidentTileCount()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return static_cast<uint32_t>(g_tiles.size());
    }

    bool GetResidentTile(uint32_t index, TileStatus& out)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        uint32_t i = 0;
        for (const auto& kv : g_tiles)
        {
            if (i++ == index)
            {
                FillStatus(*kv.second, out);
                return true;
            }
        }
        return false;
    }

    bool FindTile(int tileFirst, int tileSecond, TileStatus& out)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const auto& kv : g_tiles)
        {
            if (kv.second->tileFirst == tileFirst && kv.second->tileSecond == tileSecond)
            {
                FillStatus(*kv.second, out);
                return true;
            }
        }
        return false;
    }
}

WXL_REGISTER_FEATURE("adt-split", wxl::features::kAdtSplit, InstallAdtSplit)
