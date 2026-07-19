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
//
// Addresses/conventions come from _docs/re-legion/adt-335-integration.md and were re-verified
// against the 3.3.5.12340 Ghidra export (CMapArea__Load/Create/destructor, ProcessIffChunks,
// AsyncFileReadAllocObject/DestroyObject/Cancel, CMap__AllocRawAreaData/FreeRawAreaData).

#include "config.hpp"
#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"
#include "engine/events/Event.hpp"
#include "features/adtsplit/AdtSplit.hpp"

#include "common/Log.hpp"
#include "game/Binding.hpp"
#include "offsets/engine/Io.hpp"
#include "offsets/game/ADT.hpp"
#include "offsets/game/World.hpp"

#include <windows.h>

#include <atomic>
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

        // parked tile-level foreigners
        uint8_t* mtxp = nullptr; uint32_t mtxpSize = 0;    // into the resident _tex0 buffer
        uint8_t  mampValue = 0;                            // MHDR+0x30 / MAMP byte (zeroed for 3.3.5)
        uint32_t mclvChunks = 0, hiResHoleChunks = 0;
    };

    std::mutex g_mutex; // guards the two maps below (loads are main-thread; Lua reads snapshot)
    std::unordered_map<void*, std::unique_ptr<SplitTile>> g_tiles;   // key: CMapArea*
    std::unordered_map<std::string, bool> g_splitMaps;               // key: "<dir>\<name>" prefix

    std::atomic<uint32_t> g_statSplitMaps{ 0 }, g_statTilesLoaded{ 0 }, g_statTilesResident{ 0 },
        g_statChunksFilled{ 0 }, g_statMcrfBytes{ 0 }, g_statMtxpTiles{ 0 },
        g_statMclvChunks{ 0 }, g_statHoleChunks{ 0 }, g_statFailures{ 0 };

    adt::TileAreaLoadFn           g_origTileAreaLoad    = nullptr;
    adt::ChunkProcessIffChunksFn  g_origProcessIff      = nullptr;
    adt::TileAreaDestroyFn        g_origTileAreaDestroy = nullptr;

    SplitTile* FindTileLocked(void* area)
    {
        auto it = g_tiles.find(area);
        return it != g_tiles.end() ? it->second.get() : nullptr;
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
                else if (tag == FourCC("MTEX")) mtex = hdr;
                else if (tag == FourCC("MTXF")) mtxf = hdr;
                else if (tag == FourCC("MTXP")) { t.mtxp = hdr + 8; t.mtxpSize = sz; }        // parked
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

        // --- MCRF fixup pool: refs must be ONE contiguous u32 array, doodads first then wmos.
        // When only one side exists the split payload is aliased directly (zero copy).
        uint32_t poolBytes = 0;
        for (uint32_t i = 0; i < t.chunkCount; ++i)
        {
            ChunkFill& f = t.chunks[i];
            if (f.mcrdSize && f.mcrwSize) poolBytes += f.mcrdSize + f.mcrwSize;
        }
        t.mcrfPool.resize(poolBytes);
        uint32_t poolOff = 0;
        for (uint32_t i = 0; i < t.chunkCount; ++i)
        {
            ChunkFill& f = t.chunks[i];
            f.nDoodadRefs = f.mcrdSize / 4u;
            f.nMapObjRefs = f.mcrwSize / 4u;
            if (f.mcrdSize && f.mcrwSize)
            {
                uint8_t* dst = t.mcrfPool.data() + poolOff;
                std::memcpy(dst, f.mcrd, f.mcrdSize);
                std::memcpy(dst + f.mcrdSize, f.mcrw, f.mcrwSize);
                f.mcrf = dst;
                poolOff += f.mcrdSize + f.mcrwSize;
            }
            else if (f.mcrdSize) f.mcrf = f.mcrd;
            else if (f.mcrwSize) f.mcrf = f.mcrw;
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
            if (t->texOwned && t->texBuf) FreeRaw(t->texBuf, t->texSize);
            if (t->objOwned && t->objBuf) FreeRaw(t->objBuf, t->objSize);
            if (freeRootAfter)            FreeRaw(t->rootBuf, t->rootSize);
            g_statTilesResident.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    // ---------------------------------------------------------------- install
    /**
     * @brief Installs the three split-ADT detours (load seam, per-chunk fill seam, teardown seam).
     */
    bool InstallAdtSplit()
    {
        wxl::hook::Install("AdtSplit_TileAreaLoad", adt::kTileAreaLoad,
                           &hkTileAreaLoad, &g_origTileAreaLoad);
        wxl::hook::Install("AdtSplit_ProcessIffChunks", adt::kChunkProcessIffChunks,
                           &hkProcessIffChunks, &g_origProcessIff);
        wxl::hook::Install("AdtSplit_TileAreaDestroy", adt::kTileAreaDestroy,
                           &hkTileAreaDestroy, &g_origTileAreaDestroy);
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
        return s;
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
