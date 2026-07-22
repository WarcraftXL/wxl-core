// Native modern-M2 reader: the 3.3.5 client READS a Legion MD21 container and direct-fills CM2Shared.
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

// MECHANISM:
//
//   Detour      The existing kInit detour (m2compat hkM2Init, 0x83CF00) routes here when the
//               resident load buffer starts with the raw 'MD21' chunk tag. A stock MD20 v264
//               model takes the untouched original parser -- the modern path costs stock models
//               one magic compare. The stock parser is NEVER called for an MD21 model; this
//               module replaces its orchestration entirely.
//
//   Demux       One chunk walk over the resident container harvests the auxiliary chunks into
//               POD locals (TXID texture FileDataIDs, SFID skin FileDataIDs, presence bits for
//               the chunks Phase 1 parks: TXAC/LDV1/AFID/SKID/...). The MD20 body is then slid
//               to the buffer base IN PLACE (memmove, normally 8 bytes down) so model+0x150 --
//               which the whole engine dereferences as the parsed header -- lands on the header
//               while the ALLOCATION POINTER stays exactly what the model destructor's free
//               (and the m2memory arena's exact-pointer map) expects. Nothing is reshaped: the
//               body keeps its modern strides and its modern inner version (272-274) for the
//               model's whole life; the version-gate at 0x83CF51 is simply never executed.
//
//   Fill        Our own load walk over the modern body, replicating the stock CM2Shared parse
//               the stock per-field readers (offsets/game/M2.hpp kRead*) are driven in the stock
//               order over every record type whose stride is IDENTICAL in 272-274 -- which for a
//               doodad is everything -- converting M2Array offsets to raw pointers into the
//               resident body. The genuinely modern deltas are applied by this module first, in
//               place, per the delta list (m2-loading.md section 4.2): sequence blendTime split
//               u16 in|out -> masked to the low u16, out-of-range sequence ids remapped onto
//               client ids (lookup patched), material blend modes clamped into the client's
//               7-entry blend table, and destructor-ownership globalFlags 0x20/0x40 cleared
//               (they make the stock destructor SMemFree interior pointers). TXID FileDataIDs
//               resolve through the host DB2 authority (features/fdid, cached) and the resolved
//               path -- a process-lifetime string -- is pointed at directly by the post-fixup
//               M2Texture.filename. The stride-DIFFERENT record types a doodad does not carry
//               (cameras 0x74 vs 0x64, particles 0x1EC vs 0x1DC) are parked count=0 with a
//               logged skip (Phase 2), never fed to the stock renderer at the wrong stride.
//               Then the stock CM2Shared::Initialize (kSharedInitialize, 0x83CC80) runs -- skin
//               profile selection + the stock name-based "%s%02d.skin" load (the corpus ships
//               name-addressed skin siblings), texture-handle creation at model+0x174 from our
//               injected filename pointers -- and the stock tail is replicated (external-sequence
//               array via the real SMemAlloc, loaded flag bit 0).
//
//   Live half   The model is registered in the modern-M2 AssetRegistry (kFlagHotReshaped), so
//               the ALREADY-SHIPPING live-engine half applies unchanged: bone-budget split +
//               material/texunit contract rebuild at skin finalize (decodes the modern packed
//               shaderIds, synthesizes the textureUnitLookup a modern model ships empty),
//               hkBuildBatchMaterial guard, alpha-key ref and ribbon draw fixups.
//
//   Safety      The whole fill runs under SEH: malformed data becomes a logged failure (model
//               load fails exactly like a stock corrupt file), never a crash.

#include "config.hpp"
#include "features/m2native/M2Native.hpp"
#include "features/m2native/ParticleStride.hpp"
#include "features/fdid/Fdid.hpp"

#include "common/Log.hpp"
#include "engine/events/Event.hpp"
#include "engine/hook/Registry.hpp"
#include "engine/assets/m2/M2Format.hpp"
#include "engine/assets/models/m2/ModernM2.hpp"
#include "engine/assets/shared/models/m2/Contract.hpp"
#include "game/Binding.hpp"
#include "game/m2/M2.hpp"
#include "offsets/game/M2.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace
{
    namespace ev  = wxl::events;
    namespace fmt = wxl::structure::m2;
    namespace off = wxl::offsets::game::m2;

    // ---------------------------------------------------------------- session counters
    std::atomic<uint32_t> g_statNative{ 0 };
    std::atomic<uint32_t> g_statFailed{ 0 };
    std::atomic<uint32_t> g_statTexResolved{ 0 };
    std::atomic<uint32_t> g_statTexUnresolved{ 0 };
    std::atomic<uint32_t> g_statSkipCameras{ 0 };
    std::atomic<uint32_t> g_statSkipParticles{ 0 };
    std::atomic<uint32_t> g_statSkipTxac{ 0 };
    std::atomic<uint32_t> g_statSkipLdv1{ 0 };
    std::atomic<uint32_t> g_statSkipAfid{ 0 };
    std::atomic<uint32_t> g_statSkipSkid{ 0 };
    std::atomic<uint32_t> g_statSkipOther{ 0 };
    std::atomic<uint32_t> g_statExtSeqPending{ 0 };
    std::atomic<uint32_t> g_statShadowGateForced{ 0 };

    // ---------------------------------------------------------------- raw helpers
    inline uint32_t Rd32(const void* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

    /// MD21-container tag as the little-endian dword read of the on-disk bytes. Unique among WoW
    /// chunked formats, these tags are NOT byte-reversed ('MD21' on disk reads back 0x3132444D).
    constexpr uint32_t Tag(const char (&s)[5])
    {
        return uint32_t(uint8_t(s[0])) | (uint32_t(uint8_t(s[1])) << 8) |
               (uint32_t(uint8_t(s[2])) << 16) | (uint32_t(uint8_t(s[3])) << 24);
    }

    // skipMask bits reported in the OnM2NativeLoad event / logs.
    enum : uint32_t
    {
        kSkipTxac      = 0x01,
        kSkipLdv1      = 0x02,
        kSkipAfid      = 0x04,
        kSkipSkid      = 0x08,
        kSkipPhysBone  = 0x10, // PFID / BFID (no 3.3.5 home, permanently dropped)
        kSkipParticles = 0x20,
        kSkipCameras   = 0x40,
        kSkipOther     = 0x80, // any other auxiliary chunk (EXP2, PFDC, ...)
    };

    constexpr uint32_t kMaxTxid = 128; // corpus max is 7 textures; hard cap for the POD copy

    /// Everything harvested from the container walk, POD so it lives inside the SEH frame.
    struct Scan
    {
        uint32_t bodyOff;             // MD20 body offset within the container
        uint32_t bodySize;
        uint32_t txid[kMaxTxid];
        uint32_t txidCount;
        uint32_t sfidFirst;
        uint32_t sfidCount;
        uint32_t skipMask;
    };

    /// POD outcome of the guarded core, consumed by NativeLoad for stats/event/logging.
    struct Outcome
    {
        int      ok;
        uint32_t version;
        uint32_t texResolved;
        uint32_t texUnresolved;
        uint32_t skipMask;
        uint32_t extSeqPending;
        uint32_t shadowGateForced; // 1 when CM2Shared+0x198 had to be lifted off zero
        uint32_t shadowGateAfter;  // CM2Shared+0x198 read back immediately after the write
        const char* fail; // static failure reason when ok == 0
    };

    // ---------------------------------------------------------------- container demux
    /**
     * @brief Walks the MD21 container, harvesting the body location and the auxiliary chunks.
     * @param buf   resident container bytes.
     * @param size  container byte size.
     * @param s     receives the harvest.
     * @return true when an MD20 body large enough for a full header was found.
     */
    bool ScanContainer(const uint8_t* buf, uint32_t size, Scan& s)
    {
        std::memset(&s, 0, sizeof s);
        uint32_t offV = 0;
        while (offV + 8 <= size)
        {
            const uint32_t tag = Rd32(buf + offV);
            const uint32_t sz  = Rd32(buf + offV + 4);
            if (sz > size || offV + 8 + sz > size) break; // malformed tail; keep what we have
            const uint8_t* payload = buf + offV + 8;

            switch (tag)
            {
            case fmt::kMagicMD21: // the MD20 body itself
                s.bodyOff  = offV + 8;
                s.bodySize = sz;
                break;
            case Tag("TXID"):
            {
                uint32_t n = sz / 4;
                if (n > kMaxTxid) n = kMaxTxid;
                for (uint32_t i = 0; i < n; ++i) s.txid[i] = Rd32(payload + i * 4);
                s.txidCount = n;
                break;
            }
            case Tag("SFID"):
                s.sfidCount = sz / 4;
                if (s.sfidCount) s.sfidFirst = Rd32(payload);
                break;
            case Tag("TXAC"): s.skipMask |= kSkipTxac; break;
            case Tag("LDV1"): s.skipMask |= kSkipLdv1; break;
            case Tag("AFID"): s.skipMask |= kSkipAfid; break;
            case Tag("SKID"): s.skipMask |= kSkipSkid; break;
            case Tag("PFID"):
            case Tag("BFID"): s.skipMask |= kSkipPhysBone; break;
            default:          s.skipMask |= kSkipOther; break;
            }
            offV += 8 + sz;
        }
        return s.bodyOff != 0 && s.bodySize >= sizeof(fmt::M2Header) &&
               s.bodyOff + s.bodySize <= size;
    }

    // ---------------------------------------------------------------- pre-fixup field deltas
    // These run on the RAW body (M2Array offsets still body-relative), before the pointer walk.
    // They are this module's own expression of the delta list (m2-loading.md section 4.2); the
    // shared downport modules encode the same knowledge but are never invoked here.

    /// Finds the index of the first sequence whose id equals anim (Animations.cpp semantics).
    int16_t SequenceIndexById(const fmt::M2Sequence* seqs, uint32_t count, uint16_t anim)
    {
        for (uint32_t i = 0; i < count; ++i)
            if (seqs[i].id == anim) return static_cast<int16_t>(i);
        return -1;
    }

    /// Points lookup[newId] at newPos if it still holds oldPos, else rewrites the first entry
    /// holding oldPos (Animations.cpp semantics).
    void PatchSequenceLookup(int16_t* lookup, uint32_t lookupCount, int16_t oldPos, uint16_t newId,
                             int16_t newPos)
    {
        if (!lookup) return;
        if (newId < lookupCount && lookup[newId] == oldPos) { lookup[newId] = newPos; return; }
        for (uint32_t i = 0; i < lookupCount; ++i)
            if (lookup[i] == oldPos) { lookup[i] = newPos; break; }
    }

    /**
     * @brief Sequence deltas in place: masks the split u16 in|out blendTime to the client's single
     *        u32 (read whole it is a huge blend so transitions never complete) and remaps the
     *        curated set of source ids above the client's id table onto client ids, patching the
     *        lookup. Counts the sequences whose data streams from a .anim file (flags bit 0x20
     *        clear -- Phase 2) into extSeqPending.
     */
    void FixSequencesRaw(uint8_t* base, uint32_t size, fmt::M2Header* h, uint32_t& extSeqPending)
    {
        constexpr uint16_t kClientMaxAnimId = 505;
        if (!h->sequences.count || !h->sequences.offset) return;
        if (h->sequences.offset > size ||
            h->sequences.count * sizeof(fmt::M2Sequence) > size - h->sequences.offset)
            return; // walk will reject it
        auto* seqs = reinterpret_cast<fmt::M2Sequence*>(base + h->sequences.offset);

        int16_t* lookup = nullptr;
        uint32_t lookupCount = 0;
        if (h->sequenceLookup.count && h->sequenceLookup.offset &&
            h->sequenceLookup.offset <= size &&
            h->sequenceLookup.count * 2 <= size - h->sequenceLookup.offset)
        {
            lookup      = reinterpret_cast<int16_t*>(base + h->sequenceLookup.offset);
            lookupCount = h->sequenceLookup.count;
        }

        for (uint32_t i = 0; i < h->sequences.count; ++i)
        {
            const uint16_t id = seqs[i].id;
            if (id > kClientMaxAnimId)
            {
                uint16_t anim = id;
                switch (id)
                {
                    case 564: anim = 37;  break;
                    case 548: anim = 41;  break;
                    case 556: anim = 42;  break;
                    case 552: anim = 43;  break;
                    case 554: anim = 44;  break;
                    case 562: anim = 45;  break;
                    case 572: anim = 39;  break;
                    case 574: anim = 187; break;
                }
                if (anim != id)
                {
                    PatchSequenceLookup(lookup, lookupCount,
                                        SequenceIndexById(seqs, h->sequences.count, anim), anim,
                                        static_cast<int16_t>(i));
                    seqs[i].id = anim;
                }
            }
            seqs[i].blendTime &= 0xFFFFu;
            if (!(seqs[i].flags & 0x20u)) ++extSeqPending;
        }
    }

    /**
     * @brief Material deltas in place: a blend mode above the client's 7-entry blend table is
     *        clamped to Add (4, flags forced |0x5) and flags are masked to the low 5 bits --
     *        the same FixRenderFlags contract the skin-finalize half re-applies idempotently.
     */
    void FixMaterialsRaw(uint8_t* base, uint32_t size, fmt::M2Header* h)
    {
        if (!h->materials.count || !h->materials.offset) return;
        if (h->materials.offset > size || h->materials.count * 4 > size - h->materials.offset)
            return;
        auto* mats = reinterpret_cast<uint16_t*>(base + h->materials.offset);
        for (uint32_t i = 0; i < h->materials.count; ++i)
        {
            uint16_t& flag  = mats[i * 2 + 0];
            uint16_t& blend = mats[i * 2 + 1];
            if (blend > 6) { blend = 4; flag |= 0x5; }
            flag &= 0x1F;
        }
    }

    // ---------------------------------------------------------------- the stock load walk
    /**
     * @brief Drives the stock per-field offset->pointer readers over the modern body, in the stock
     *        parse order (RE'd from CM2Shared__FinishLoading). Every reader used here covers a
     *        record type whose stride is identical between 264 and 272-274; cameras and particles
     *        (modern-wider strides) were parked count=0 by the caller and are skipped.
     * @return true when every reader accepted its array (bounds-valid offsets).
     */
    bool DriveStockWalk(uint8_t* base, uint32_t size, fmt::M2Header* h)
    {
        auto rd = [&](uintptr_t fn, void* arr) -> bool {
            return wxl::game::Native<off::M2_HeaderReadFn>(fn)(base, size, h, arr) != 0;
        };

        if (!rd(off::kReadByteArray,    &h->name)) return false;
        if (!rd(off::kReadInt32Array,   &h->globalLoops)) return false;
        if (!rd(off::kReadAnimations,   &h->sequences)) return false;
        if (!rd(off::kReadInt16Array,   &h->sequenceLookup)) return false;
        if (!rd(off::kReadBones,        &h->bones)) return false; // reads the fixed-up sequences
        if (!rd(off::kReadInt16Array,   &h->boneLookup)) return false;
        if (!rd(off::kReadVertices,     &h->vertices)) return false;
        if (!rd(off::kReadColors,       &h->colors)) return false;
        if (!rd(off::kReadTextures,     &h->textures)) return false;
        if (!rd(off::kReadTransparency, &h->textureWeights)) return false;
        if (!rd(off::kReadUVAnimation,  &h->textureTransforms)) return false;
        if (!rd(off::kReadInt16Array,   &h->textureReplacements)) return false;
        if (!rd(off::kReadInt32Array,   &h->materials)) return false;
        if (!rd(off::kReadInt16Array,   &h->boneCombos)) return false;
        if (!rd(off::kReadInt16Array,   &h->textureCombos)) return false;
        if (!rd(off::kReadInt16Array,   &h->textureUnitLookup)) return false;
        if (!rd(off::kReadInt16Array,   &h->textureWeightCombos)) return false;
        if (!rd(off::kReadInt16Array,   &h->textureTransformCombos)) return false;
        if (!rd(off::kReadInt16Array,   &h->collisionIndices)) return false;
        if (!rd(off::kReadVector3,      &h->collisionPositions)) return false;
        if (!rd(off::kReadVector3,      &h->collisionNormals)) return false;
        if (!rd(off::kReadAttachments,  &h->attachments)) return false;
        if (!rd(off::kReadInt16Array,   &h->attachmentLookup)) return false;
        if (!rd(off::kReadEvents,       &h->events)) return false;
        if (!rd(off::kReadLights,       &h->lights)) return false;
        // cameras + cameraLookup parked (count 0) by the caller; the stock 264-stride camera
        // reader must never touch a 0x74-stride modern record.
        if (!rd(off::kRibbonDeRelocate, &h->ribbonEmitters)) return false; // stride 0xB0, identical
        // particleEmitters: the caller left them populated (count != 0) only when the client can step
        // the modern 0x1EC stride -- ParticleStride.cpp patched the reader's stride sites. A parked
        // array is count 0 and the stock reader is a no-op on it, so this is safe either way.
        if (h->particleEmitters.count)
            if (!rd(off::kReadParticleEmitters, &h->particleEmitters)) return false;
        // The two modern emitter encodings the stock client can't read (packed multi-texture
        // textureId, blend mode 7) are handled by TEACHING THE CLIENT to read them --
        // features/m2native/ParticleStride.cpp patches the read sites and the blend table. The record
        // itself is left exactly as the file has it; we never rewrite emitter data.
        if (h->globalFlags & fmt::kFlagUseTextureCombinerCombos)
            if (!rd(off::kReadInt16Array, &h->textureCombinerCombos)) return false;
        return true;
    }

    // ---------------------------------------------------------------- post-fixup injections
    /**
     * @brief Points each hardcoded (type 0) texture with no inline name at its TXID-resolved
     *        client path. Post-fixup, M2Texture.filename.offset is a raw pointer, so it can aim
     *        directly at the resolver's process-lifetime cached string -- no buffer growth. The
     *        stock CM2Shared::Initialize then TextureCreate()s exactly that path; an unresolved
     *        id keeps count 0 and falls back to the stock solid-white placeholder.
     */
    void InjectTxidNames(fmt::M2Header* h, const Scan& s, Outcome& out)
    {
        if (!h->textures.count || !h->textures.offset) return;
        auto* tex = reinterpret_cast<fmt::M2Texture*>(static_cast<uintptr_t>(h->textures.offset));
        for (uint32_t i = 0; i < h->textures.count; ++i)
        {
            if (tex[i].type != fmt::kTexTypeHardcoded) continue;   // dynamic slots stay empty
            if (tex[i].filename.count >= 2 && tex[i].filename.offset) continue; // inline name kept
            const uint32_t fdid = i < s.txidCount ? s.txid[i] : 0;
            if (!fdid) continue;
            const char* path = wxl::fdid::ResolveTexture(fdid);
            if (!path)
            {
                ++out.texUnresolved;
                WLOG_WARN("m2native: TXID %u unresolved (texture %u) -- solid white fallback", fdid, i);
                continue;
            }
            tex[i].filename.offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(path));
            tex[i].filename.count  = static_cast<uint32_t>(std::strlen(path)) + 1u;
            ++out.texResolved;
        }
    }

    /// Clamps each ribbon's texture/material reference values into the header tables (the modern
    /// exporter can emit indices past the client tables; delta list section 4.2).
    void ClampRibbonRefs(fmt::M2Header* h)
    {
        if (!h->ribbonEmitters.count || !h->ribbonEmitters.offset) return;
        auto* rib = reinterpret_cast<fmt::M2Ribbon*>(static_cast<uintptr_t>(h->ribbonEmitters.offset));
        for (uint32_t i = 0; i < h->ribbonEmitters.count; ++i)
        {
            auto clamp = [](const fmt::M2Array& a, uint32_t limit) {
                if (!a.count || !a.offset || !limit) return;
                auto* v = reinterpret_cast<uint16_t*>(static_cast<uintptr_t>(a.offset));
                for (uint32_t n = 0; n < a.count; ++n)
                    if (v[n] >= limit) v[n] = static_cast<uint16_t>(limit - 1);
            };
            clamp(rib[i].textureIndices, h->textures.count);
            clamp(rib[i].materialIndices, h->materials.count);
        }
    }

    // ---------------------------------------------------------------- the native fill core
    /**
     * @brief The full native load: demux, in-place deltas, stock walk, TXID injection, stock
     *        CM2Shared::Initialize, stock tail. POD locals only (lives under the SEH guard).
     * @param model  runtime model whose buffer holds the raw MD21 bytes.
     * @param out    receives the outcome for stats / the OnM2NativeLoad event.
     */
    void NativeLoadCore(void* model, Outcome& out)
    {
        auto* mdl = static_cast<off::M2Model*>(model);
        if (mdl->flags & 1u)
        {
            // Stock re-entry contract: already loaded, return success. Re-register (the pre-load
            // event Forgets the pointer defensively on every entry) and skip the stats/event.
            wxl::modern::assets::m2::RegisterNativeLoaded(model);
            out.ok = 2;
            return;
        }

        auto* buf = static_cast<uint8_t*>(mdl->header);
        const uint32_t size = mdl->fileSize;
        if (!buf || size < 8) { out.fail = "no buffer"; return; }

        Scan s;
        if (!ScanContainer(buf, size, s)) { out.fail = "no MD20 body in container"; return; }
        out.skipMask = s.skipMask;

        auto* h = reinterpret_cast<fmt::M2Header*>(buf + s.bodyOff);
        if (h->magic != fmt::kMagicMD20) { out.fail = "body magic"; return; }
        out.version = h->version;
        if (h->version < wxl::modern::assets::m2::kSourceVersionMin ||
            h->version > wxl::modern::assets::m2::kSourceVersionMax)
        {
            out.fail = "inner version outside the supported source window";
            return;
        }

        // A split-skeleton model (SKID / empty bone+sequence arrays) has no Phase-1 home: without
        // the .skel splice it would stand unboned. Refuse the load (a clean miss, like an absent
        // file) rather than filling a broken runtime. Phase 3 lifts this.
        if ((s.skipMask & kSkipSkid) || (!h->bones.count && !h->sequences.count))
        {
            out.fail = "split skeleton (.skel) model -- Phase 3";
            return;
        }

        // Slide the body onto the allocation base (chunk-header bytes ahead of it are dead after
        // the harvest; trailing chunks are beyond the moved range and already harvested). The
        // model keeps its original allocation pointer -- the destructor free and the m2memory
        // arena's exact-pointer bookkeeping stay valid -- and +0x150 now IS the header.
        if (s.bodyOff != 0)
            std::memmove(buf, buf + s.bodyOff, s.bodySize);
        wxl::game::m2::ReplaceBuffer(model, buf, s.bodySize);
        h = reinterpret_cast<fmt::M2Header*>(buf);

        // --- in-place field deltas on the raw body (delta list, m2-loading.md section 4.2) ---
        // Modern bits 0x20/0x40 tell the 3.3.5 destructor "the runtime owns textureCombos /
        // textureTransformCombos" and make it SMemFree interior pointers of this buffer.
        h->globalFlags &= ~0x60u;
        FixSequencesRaw(buf, s.bodySize, h, out.extSeqPending);
        FixMaterialsRaw(buf, s.bodySize, h);

        // Park cameras (modern 0x74 stride vs client 0x64): the stock camera reader would walk a
        // modern record at the wrong stride. Zeroed unconditionally so the post-parse header state
        // stays bit-faithful to a stock parse, and the walk below never visits them.
        if (h->cameras.count) out.skipMask |= kSkipCameras;
        h->cameras      = fmt::M2Array{ 0, 0 };
        h->cameraLookup = fmt::M2Array{ 0, 0 };

        // Particle emitters keep their modern 0x1EC stride and are read in place once the client has
        // been taught that stride (ParticleStride.cpp redirects the nine hardcoded-stride sites).
        //
        // They stay PARKED for now regardless, because stride is not the only thing the stock client
        // misreads in a modern record: with flag 0x10000000 the textureId at +0x16 is three packed
        // 5-bit ids, and the client uses it as a flat index into the model's texture-handle table --
        // InitializeLoaded then AddRefs table[hugeId], an out-of-bounds read that faults. Teaching the
        // client to unpack at its read sites is the correct fix (never rewrite the record); until that
        // patch is in and verified, parking is the honest state: no fire, but no crash and no data touched.
        const bool particlesReadable = wxl::runtime::m2particles::Installed() &&
                                       wxl::runtime::m2particles::TextureIdSitesPatched();
        if (h->particleEmitters.count && !particlesReadable)
        {
            out.skipMask |= kSkipParticles;
            h->particleEmitters = fmt::M2Array{ 0, 0 };
        }

        // The stock skin chooser walks a 4-entry threshold table indexed 4 - numSkinProfiles;
        // more profiles than the client ever shipped (LDV1-era LOD counts) would underflow it.
        if (h->numSkinProfiles > 4)
        {
            out.skipMask |= kSkipLdv1;
            h->numSkinProfiles = 1; // profile 0 = full detail; LOD chains are Phase 2
        }

        // --- the stock offset->pointer walk, at the (identical) modern strides ---
        if (!DriveStockWalk(buf, s.bodySize, h)) { out.fail = "header walk rejected an array"; return; }

        // --- post-fixup injections on the now-pointer-based header ---
        InjectTxidNames(h, s, out);
        ClampRibbonRefs(h);

        // Register for the live-engine half BEFORE the stock skin load can schedule its finalize:
        // the finalize-time contract rebuild (packed shaderId decode, textureUnitLookup synth) and
        // the draw fixups key off this registry.
        wxl::modern::assets::m2::RegisterNativeLoaded(model);

        // --- stock CM2Shared::Initialize: skin select + name-based skin load + texture handles ---
        if (!wxl::game::Native<off::M2_SharedInitializeFn>(off::kSharedInitialize)(model))
        {
            out.fail = "CM2Shared::Initialize failed (skin profile / texture array)";
            return;
        }

        // Shadow-path animate gate. Initialize has just tallied, at +0x198, how many bones carry
        // flags & 0x2F8. Its only reader is CM2Model::AnimateSM (0x00831990), the SHADOW-path animate:
        // with the count at zero it takes a fast path that never rebuilds the bone palette, so the
        // palette still holds an older frame's view matrix while CShadowQuery::Render uploads
        // c14..c16 from the CURRENT view -- the two stop cancelling and the shadow rotates with the
        // camera. WotLK exporters bake 0x200 into practically every bone so stock content always takes
        // the full path; a Legion/Midnight model ships every bone flag at 0x0 and would not.
        // Forcing the count to 1 puts our models on the exact path stock content already takes. This
        // writes the RUNTIME object, never the model bytes: the bone flags stay what the file says.
        auto* animGate = reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(model) +
                                                     off::kOffSharedAnimGateCount);
        if (*animGate == 0)
        {
            *animGate = 1;
            out.shadowGateForced = 1;
        }
        // Read back so the log can tell "the write never ran" apart from "the write ran and something
        // later reset it" -- the probe sees 0 at the shadow draw, and only this distinguishes the two.
        out.shadowGateAfter = *animGate;

        // --- stock tail (RE'd from CM2Shared__FinishLoading): the external-sequence table the
        // .anim streamer indexes, then the loaded flag ---
        uint32_t extCount = 0;
        if (h->sequences.count && h->sequences.offset)
        {
            auto* seqs = reinterpret_cast<fmt::M2Sequence*>(static_cast<uintptr_t>(h->sequences.offset));
            for (uint32_t i = 0; i < h->sequences.count; ++i)
                if (!(seqs[i].flags & 0x20u)) ++extCount;
        }
        void* extArr = wxl::game::Native<off::SMemAllocFn>(off::kSMemAlloc)(
            extCount * 4u, ".\\M2Shared.cpp", 0x2DC, 8);
        auto* bytes = static_cast<uint8_t*>(model);
        *reinterpret_cast<void**>(bytes + off::kOffModelExtSeqArray)     = extArr;
        *reinterpret_cast<uint32_t*>(bytes + off::kOffModelExtSeqCount)  = extCount;
        mdl->flags |= 1u;
        *reinterpret_cast<uint32_t*>(bytes + off::kOffModelExtSeqCursor) = 0;

        out.ok = 1;
    }

    /// SEH shell around the fill: a fault on malformed data becomes a logged failure, never a
    /// crash. No unwindable locals here (C2712).
    void NativeLoadGuarded(void* model, Outcome* out)
    {
        __try
        {
            NativeLoadCore(model, *out);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out->ok   = 0;
            out->fail = "access violation during native fill";
        }
    }

    bool InstallM2Native()
    {
        WLOG_INFO("m2native: native MD21 reader active (source versions %u-%u, direct CM2Shared fill)",
                  wxl::modern::assets::m2::kSourceVersionMin,
                  wxl::modern::assets::m2::kSourceVersionMax);
        return true;
    }
}

namespace wxl::runtime::m2native
{
    Stats GetStats()
    {
        Stats s{};
        s.modelsNative       = g_statNative.load(std::memory_order_relaxed);
        s.modelsFailed       = g_statFailed.load(std::memory_order_relaxed);
        s.texturesResolved   = g_statTexResolved.load(std::memory_order_relaxed);
        s.texturesUnresolved = g_statTexUnresolved.load(std::memory_order_relaxed);
        s.skippedCameras     = g_statSkipCameras.load(std::memory_order_relaxed);
        s.skippedParticles   = g_statSkipParticles.load(std::memory_order_relaxed);
        s.skippedTxac        = g_statSkipTxac.load(std::memory_order_relaxed);
        s.skippedLdv1        = g_statSkipLdv1.load(std::memory_order_relaxed);
        s.skippedAfid        = g_statSkipAfid.load(std::memory_order_relaxed);
        s.skippedSkid        = g_statSkipSkid.load(std::memory_order_relaxed);
        s.skippedOtherChunks = g_statSkipOther.load(std::memory_order_relaxed);
        s.externalSeqPending = g_statExtSeqPending.load(std::memory_order_relaxed);
        s.shadowGateForced   = g_statShadowGateForced.load(std::memory_order_relaxed);
        return s;
    }

    bool Enabled() { return wxl::features::kNativeM2; }

    bool IsModernContainer(void* model)
    {
        if (!model) return false;
        auto* mdl = static_cast<off::M2Model*>(model);
        if (!mdl->header || mdl->fileSize < 8) return false;
        return Rd32(mdl->header) == fmt::kMagicMD21;
    }

    int NativeLoad(void* model)
    {
        Outcome out{};
        NativeLoadGuarded(model, &out);
        if (out.ok == 2) return 1; // already-loaded re-entry: no stats, no event

        const char* stem = wxl::game::m2::PathStem(model);
        if (!stem) stem = "(no stem)";

        if (!out.ok)
        {
            g_statFailed.fetch_add(1, std::memory_order_relaxed);
            wxl::modern::assets::m2::ForgetNativeLoaded(model); // undo a pre-Initialize registration
            WLOG_WARN("m2native: '%s' native fill FAILED: %s (v=%u skips=0x%X)",
                      stem, out.fail ? out.fail : "unknown", out.version, out.skipMask);
            return 0;
        }

        g_statNative.fetch_add(1, std::memory_order_relaxed);
        g_statTexResolved.fetch_add(out.texResolved, std::memory_order_relaxed);
        g_statTexUnresolved.fetch_add(out.texUnresolved, std::memory_order_relaxed);
        if (out.skipMask & kSkipCameras)   g_statSkipCameras.fetch_add(1, std::memory_order_relaxed);
        if (out.skipMask & kSkipParticles) g_statSkipParticles.fetch_add(1, std::memory_order_relaxed);
        if (out.skipMask & kSkipTxac)      g_statSkipTxac.fetch_add(1, std::memory_order_relaxed);
        if (out.skipMask & kSkipLdv1)      g_statSkipLdv1.fetch_add(1, std::memory_order_relaxed);
        if (out.skipMask & kSkipAfid)      g_statSkipAfid.fetch_add(1, std::memory_order_relaxed);
        if (out.skipMask & kSkipSkid)      g_statSkipSkid.fetch_add(1, std::memory_order_relaxed);
        if (out.skipMask & kSkipOther)     g_statSkipOther.fetch_add(1, std::memory_order_relaxed);
        g_statExtSeqPending.fetch_add(out.extSeqPending, std::memory_order_relaxed);
        g_statShadowGateForced.fetch_add(out.shadowGateForced, std::memory_order_relaxed);

        WLOG_INFO("m2native: '%s' read natively (v=%u tex=%u/%u skips=0x%X extseq=%u gate=%u forced=%u)",
                  stem, out.version, out.texResolved, out.texResolved + out.texUnresolved,
                  out.skipMask, out.extSeqPending, out.shadowGateAfter, out.shadowGateForced);
        if (out.extSeqPending)
            WLOG_INFO("m2native: '%s' has %u streamed (.anim) sequence(s) -- Phase 2, bind pose until then",
                      stem, out.extSeqPending);

        ev::M2NativeLoadArgs a{ model, out.version, out.texResolved, out.texUnresolved, out.skipMask };
        ev::Emit(ev::Event::OnM2NativeLoad, &a);
        return 1;
    }
}

WXL_REGISTER_FEATURE("m2native", wxl::features::kNativeM2, InstallM2Native)
