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

// MECHANISM (read where it lies, adapt the client)
//
//   Classify   A root buffer is walked once by tag. It is MODERN when it carries no MOTX or carries
//              GFID -- the two facts that make the positional walker desynchronise. Stock roots take
//              the untouched original walker, so a mixed session (stock Azeroth + modern map) is
//              correct. The verdict is recorded per root object and OVERWRITTEN on every root load,
//              so a pooled root slot reused by a stock WMO can never inherit a stale modern verdict.
//
//   Root       Our walker fills the same 17 slots the stock one does (offsets/game/WMO.hpp
//              kRootSlots), matching on the tag instead of the position, and reproduces the three
//              non-trivial things the stock walker does: the MOSB emptiness test, the resulting
//              in-place MOGI sky-flag clear, and the MOPT NaN plane repair. Chunks with no 3.3.5 home
//              (GFID, MODI, MGI2, MDDI, MNLD, MFED, MAVG, ...) are counted and skipped -- skipping a
//              chunk we cannot place is exactly what a tag-driven walk is for; nothing is rewritten.
//
//   Group      Same shape over kGroupSlots, merging the mandatory and optional walkers into one pass.
//              The flag bits the stock optional walker gates on are not consulted: presence decides,
//              which is what makes the modern group's `0x400` LOD bit harmless (the stock walker would
//              blind-skip four chunks that are not there and wreck every pointer after it). MOTV and
//              MOCV legitimately appear twice; the second occurrence feeds the "2nd set" slot. MOBN +
//              MOBR are handed to the stock BSP init, and the MOCV alpha rewrite runs under the same
//              MOHD gate as stock.
//
//   Materials  A modern MOMT addresses its textures by FileDataID and the root has no MOTX, so the
//              stock CreateMaterial would add an id to a null base. For a modern root we resolve both
//              ids through the existing FileDataID service and drive the stock texture loader
//              ourselves, reproducing CreateMaterial's own contract (idempotent handle guard, empty
//              texture_1 fallback, shader 3/5/6 -> 4 collapse, second texture dropped when the shader
//              pipeline is off).
//
//   Parked     What 3.3.5 has no representation for is RETAINED and counted, never rewritten: MODD
//              when the root ships MODI (doodad ids became an index, and there is no MODN name blob to
//              resolve against), MLIQ (modern liquid type ids are far beyond LiquidType.dbc and the
//              client dereferences a null record), MOVX (32-bit indices). Parking keeps the WMO
//              loadable; each counter is the todo list for the next phase.
//
// The MOBA material id is NOT handled here. A modern batch carries it as a u16 at +0x0A with bit 0x02
// set at +0x16, while the client reads a byte at +0x17 (zero in these files) at nine separate sites,
// six of them on the render path. Until that is settled every batch resolves material 0.

#include "config.hpp"

#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"
#include "features/fdid/Fdid.hpp"
#include "features/wmonative/WmoNative.hpp"

#include "common/Log.hpp"
#include "game/Binding.hpp"
#include "offsets/game/ADT.hpp"
#include "offsets/game/WMO.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace
{
    namespace off = wxl::offsets::game::wmo;
    namespace adt = wxl::offsets::game::adt;

    // ------------------------------------------------------------------ small raw-memory helpers
    inline uint32_t Rd32(const void* p)
    {
        uint32_t v;
        std::memcpy(&v, p, 4);
        return v;
    }

    inline void Wr32(void* p, uint32_t v) { std::memcpy(p, &v, 4); }

    inline uint8_t* Field(void* obj, size_t offset)
    {
        return static_cast<uint8_t*>(obj) + offset;
    }

    inline void SetPtr(void* obj, size_t offset, const void* value)
    {
        Wr32(Field(obj, offset), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value)));
    }

    inline void SetU32(void* obj, size_t offset, uint32_t value) { Wr32(Field(obj, offset), value); }

    inline void* GetPtr(void* obj, size_t offset)
    {
        return reinterpret_cast<void*>(static_cast<uintptr_t>(Rd32(Field(obj, offset))));
    }

    /**
     * @brief Walks a chunk container by tag.
     *
     * A WMO tag is stored reversed on disk, so the four bytes read back as one LE dword are exactly
     * what off::WmoTag builds -- the dword is compared directly, with no byte shuffling. The walk stops
     * at the first header that would run past `end` or whose payload does not fit, which is the one
     * safety the stock walker lacks.
     * @param begin  first chunk header.
     * @param end    one past the last readable byte.
     * @param fn     invoked as fn(tag, content, size) per chunk.
     */
    template <class Fn>
    void WalkChunks(uint8_t* begin, uint8_t* end, Fn&& fn)
    {
        uint8_t* p = begin;
        while (p + 8 <= end)
        {
            const uint32_t tag  = Rd32(p);
            const uint32_t size = Rd32(p + 4);
            uint8_t* content = p + 8;
            if (size > static_cast<uint32_t>(end - content))
                break;
            fn(tag, content, size);
            p = content + size;
        }
    }

    /// Modern-only chunks: recognised so they are counted rather than reported as unknown.
    bool IsKnownModernOnly(uint32_t tag)
    {
        static const uint32_t kTags[] = {
            off::WmoTag("GFID"), off::WmoTag("MODI"), off::WmoTag("MGI2"), off::WmoTag("MDDI"),
            off::WmoTag("MNLD"), off::WmoTag("MFED"), off::WmoTag("MAVG"), off::WmoTag("MAVD"),
            off::WmoTag("MDDL"), off::WmoTag("MPVD"), off::WmoTag("MOSI"), off::WmoTag("MOUV"),
            off::WmoTag("MOGX"), off::WmoTag("MOBS"), off::WmoTag("MOPB"), off::WmoTag("MNLR"),
            off::WmoTag("MDAL"), off::WmoTag("MFVR"), off::WmoTag("MPVR"), off::WmoTag("MAVR"),
            off::WmoTag("MPY2"), off::WmoTag("MOC2"), off::WmoTag("MOQG"),
        };
        for (uint32_t t : kTags)
            if (t == tag) return true;
        return false;
    }

    // ------------------------------------------------------------------ state
    std::atomic<uint32_t> g_rootsModern{0}, g_rootsStock{0}, g_groupsModern{0}, g_groupsFailed{0};
    std::atomic<uint32_t> g_texResolved{0}, g_texUnresolved{0};
    std::atomic<uint32_t> g_parkedDoodads{0}, g_parkedLiquids{0}, g_parkedIndex32{0}, g_unknownChunks{0};
    std::atomic<uint32_t> g_shaderRemapped{0}, g_parkedNoUv{0};
    std::atomic<uint32_t> g_shaderToTwoLayer{0}, g_shaderToEnv{0}, g_shaderToSingle{0};
    std::atomic<uint32_t> g_materialIdsMoved{0}, g_materialOutOfRange{0};
    std::atomic<uint32_t> g_batchCullBypassed{0};
    std::atomic<uint32_t> g_shaderSeen{0}; ///< bitmask of already-reported unsupported shader ids
    std::atomic<bool>     g_installed{false};

    /// Live A/B of the family remap. True (default) maps a modern shader onto the nearest native
    /// effect keeping its second diffuse layer; false reverts to the old single-layer fallback (any
    /// unsupported id -> 0). Only affects materials not yet resolved (the handle guard in CreateMaterial).
    std::atomic<bool>     g_shaderRemapEnabled{true};
    /// Live A/B of the modern vertex-colour fix. A modern WMO sets MOHD 0x08 (do_not_fix_vertex_color
    /// _alpha) because its UNIFIED SHADER folds the fix in; 3.3.5 has no such shader and MULTIPLIES
    /// MOCV into the texture, so the modern near-black MOCV (measured mean RGB ~20/255) turns every
    /// surface black. True (default) runs the stock CPU fix anyway for modern groups -- teaching the
    /// client that, lacking the unified shader, it must keep doing the fix the flag disables. False
    /// honours the flag (stock behaviour, surfaces stay black), for A/B.
    std::atomic<bool>     g_forceVertexColorFix{true};
    std::atomic<uint32_t> g_vertexColorFixed{0}; ///< modern groups whose MOCV was fixed by the client
    /// Live A/B of the near-white MOCV neutralization. A modern source leaves some exterior groups' vertex
    /// colours at a near-white PLACEHOLDER (its unified shader needs no baked lighting), while the groups
    /// that shade correctly carry near-black colours. 3.3.5 MULTIPLIES the surface by the vertex colour, so
    /// a near-white group renders almost white / over-bright. True (default) zeroes the RGB of any near-white
    /// entry so it shades from the ambient/scene light like the correct groups; darker colours are untouched.
    std::atomic<bool>     g_neutralizeNearWhite{true};
    std::atomic<uint32_t> g_mocvNeutralized{0}; ///< MOCV entries zeroed as near-white placeholders
    /// Manual UV-set probe, default 0 (untouched). The correct fix is NOT a global load-time swap -- that
    /// is whack-a-mole, because it rewrites the whole group's shared vertex data, so a group mixing single-
    /// and two-layer materials cannot be satisfied. The real fix routes per EFFECT at draw (CompositeShader)
    /// using Legion's "a material samples the last N UV sets" rule. Modes 1..6 stay for A/B diagnostics.
    std::atomic<int>      g_uvMode{0};
    std::atomic<uint32_t> g_uvTransformed{0};
    /// Pure-single-layer 2+-set groups whose base UV slot was pointed at the group's LAST set at load
    /// (Legion's "a single-layer material samples the last N UV sets" rule), so the stock one-UV vertex
    /// buffer feeds the right coordinates with no shader/vertex-declaration dependency. See WalkGroupModern.
    std::atomic<uint32_t> g_baseSetReoriented{0};

    /// Modern verdict per root object. Rewritten on every root load, so a recycled pool slot never
    /// inherits the previous occupant's verdict.
    std::mutex g_mutex;
    std::unordered_map<void*, bool> g_rootIsModern;

    void RecordRootKind(void* root, bool modern)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_rootIsModern[root] = modern;
    }

    bool RootIsModern(void* root)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_rootIsModern.find(root);
        return it != g_rootIsModern.end() && it->second;
    }

    // ------------------------------------------------------------------ root
    off::Wmo_RootWalkFn  g_origRootWalk  = nullptr;
    off::Wmo_GroupWalkFn g_origGroupWalk = nullptr;
    using CreateMaterialFn = void(__fastcall*)(void* root, void* edx, int materialIndex);
    CreateMaterialFn g_origCreateMaterial = nullptr;

    /**
     * @brief Classifies a root buffer without writing anything.
     *
     * MODERN means "the positional walker cannot read this": no MOTX (the walker's slot 2, whose
     * absence shifts every later chunk into the wrong slot) or a GFID present (a Midnight root that
     * addresses its groups by FileDataID). Both are decided by presence alone, never by MVER -- the
     * client never reads the version dword, and modern roots still declare 17.
     */
    bool ClassifyRootBuffer(uint8_t* base, uint32_t size)
    {
        bool sawMotx = false, sawGfid = false;
        WalkChunks(base, base + size, [&](uint32_t tag, uint8_t*, uint32_t) {
            if (tag == off::WmoTag("MOTX")) sawMotx = true;
            else if (tag == off::WmoTag("GFID")) sawGfid = true;
        });
        return !sawMotx || sawGfid;
    }

    /// Clears the SHOW_SKY bit on every MOGI entry, exactly as the stock walker does when the root
    /// carries no skybox name. The write lands in the file buffer because that is where MOGI lives and
    /// where the client itself performs this clear.
    void ClearMogiSkyFlags(void* root)
    {
        auto* mogi = static_cast<uint8_t*>(GetPtr(root, off::kOffMogiTable));
        if (!mogi) return;
        const uint32_t count = Rd32(Field(root, 0x16C));
        for (uint32_t i = 0; i < count; ++i)
        {
            uint8_t* entry = mogi + i * off::kMogiStride;
            Wr32(entry, Rd32(entry) & ~off::kMogiFlagShowSky);
        }
    }

    /// Repairs portal planes whose distance is NaN, exactly as the stock walker does: normal (0,0,1)
    /// and a distance far outside any map.
    void RepairPortalPlanes(void* root)
    {
        auto* mopt = static_cast<uint8_t*>(GetPtr(root, 0x138));
        if (!mopt) return;
        const uint32_t count = Rd32(Field(root, 0x174));
        for (uint32_t i = 0; i < count; ++i)
        {
            uint8_t* plane = mopt + i * 0x14;
            float distance;
            std::memcpy(&distance, plane + 0x10, 4);
            if (!(distance != distance))    // only a NaN fails this comparison with itself
                continue;
            const float normal[3] = { 0.0f, 0.0f, 1.0f };
            std::memcpy(plane + 0x04, normal, sizeof normal);
            const float repaired = off::kPortalPlaneRepairDistance;
            std::memcpy(plane + 0x10, &repaired, 4);
        }
    }

    /// Tag-driven root fill. Returns false only if the buffer is unusable.
    bool WalkRootModern(void* root)
    {
        auto* base = static_cast<uint8_t*>(GetPtr(root, off::kOffRootBuffer));
        const uint32_t size = Rd32(Field(root, off::kOffRootSize));
        if (!base || size < 12)
            return false;

        for (const auto& slot : off::kRootSlots)
        {
            SetPtr(root, slot.ptrField, nullptr);
            if (slot.countField) SetU32(root, slot.countField, 0);
        }

        uint32_t unknown = 0;
        WalkChunks(base, base + size, [&](uint32_t tag, uint8_t* content, uint32_t chunkSize) {
            for (const auto& slot : off::kRootSlots)
            {
                if (slot.tag != tag) continue;
                SetPtr(root, slot.ptrField, content);
                if (slot.countField)
                    SetU32(root, slot.countField, slot.stride == 1 ? chunkSize : chunkSize / slot.stride);
                return;
            }
            if (tag == off::WmoTag("MVER")) return;
            if (IsKnownModernOnly(tag)) { ++unknown; return; }
            ++unknown;
        });
        g_unknownChunks.fetch_add(unknown, std::memory_order_relaxed);

        // MOSB present but empty means "no skybox", same as absent -- the stock walker collapses both
        // to a null pointer and then clears the sky bit on every group.
        auto* mosb = static_cast<uint8_t*>(GetPtr(root, 0x12C));
        if (mosb && mosb[0] == '\0')
        {
            SetPtr(root, 0x12C, nullptr);
            mosb = nullptr;
        }
        if (!mosb)
            ClearMogiSkyFlags(root);

        RepairPortalPlanes(root);

        // MODD indexes MODN by byte offset. A modern root replaced MODN with MODI (an index table), so
        // there is no name blob to resolve against and the stock doodad spawn would read through a null
        // base. Park the definitions -- the data stays in the buffer for the phase that reads MODI.
        if (!GetPtr(root, 0x150) && Rd32(Field(root, 0x190)) != 0)
        {
            SetU32(root, 0x190, 0);
            SetPtr(root, 0x154, nullptr);
            g_parkedDoodads.fetch_add(1, std::memory_order_relaxed);
        }
        return true;
    }

    void __fastcall hkRootWalk(void* root, void* edx)
    {
        auto* base = static_cast<uint8_t*>(GetPtr(root, off::kOffRootBuffer));
        const uint32_t size = Rd32(Field(root, off::kOffRootSize));

        bool modern = false;
        if (base && size >= 12)
        {
            __try { modern = ClassifyRootBuffer(base, size); }
            __except (EXCEPTION_EXECUTE_HANDLER) { modern = false; }
        }
        RecordRootKind(root, modern);

        if (!modern)
        {
            g_rootsStock.fetch_add(1, std::memory_order_relaxed);
            g_origRootWalk(root, edx);
            return;
        }

        bool ok = false;
        __try { ok = WalkRootModern(root); }
        __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }

        if (!ok)
        {
            // A modern root the stock walker cannot read either: leave every slot null rather than
            // hand it wild pointers. CreateData then allocates zero groups and the WMO stays empty.
            RecordRootKind(root, false);
            WLOG_ERROR("wmo-native: root walk failed, map object left empty (path '%s')",
                       reinterpret_cast<const char*>(Field(root, off::kOffNameInline)));
            return;
        }
        g_rootsModern.fetch_add(1, std::memory_order_relaxed);
    }

    // ------------------------------------------------------------------ group
    inline uint32_t NativeShaderFor(uint32_t modern); // defined with the material walker below

    /// A modern MOBA batch carries its material index as a u16 at +0x0A (announced by flag bit 0x02 at
    /// +0x16); a stock batch keeps the client's u8 at +0x17. Read whichever this record announces.
    inline uint32_t BatchMaterialIndex(const uint8_t* rec)
    {
        if (rec[off::kOffMobaFlags] & off::kMobaFlagMaterialModern)
        {
            uint16_t m;
            std::memcpy(&m, rec + off::kOffMobaMaterialModern, 2);
            return m;
        }
        return rec[off::kOffMobaMaterial];
    }

    /// True when any batch of `group` draws a real two-layer Composite material -- i.e. when the stock
    /// finalize would `or [group+0x198],8` and build the group in the two-UV vertex format. This
    /// reproduces that whole-group decision at load: a material is Composite iff its shader maps to native
    /// effect 6 (NativeShaderFor) AND it actually ships a second texture (else CreateMaterial collapses
    /// 6 -> 4, single-layer). The raw modern shader / tex2 FileDataID are read straight from the MOMT in
    /// the root buffer, so this holds whether or not CreateMaterial has run yet.
    bool GroupHasComposite(void* group)
    {
        auto* batches = static_cast<uint8_t*>(GetPtr(group, off::kGroupSlots[5].ptrField));
        const uint32_t batchCount = Rd32(Field(group, off::kGroupSlots[5].countField));
        void* root = GetPtr(group, off::kOffGroupRoot);
        auto* materials = root ? static_cast<uint8_t*>(GetPtr(root, off::kOffMaterialBase)) : nullptr;
        const uint32_t matCount = root ? Rd32(Field(root, off::kOffMaterialCount)) : 0;
        if (!batches || !materials)
            return false;
        for (uint32_t i = 0; i < batchCount; ++i)
        {
            const uint8_t* rec = batches + static_cast<size_t>(i) * off::kMobaStride;
            const uint32_t mi = BatchMaterialIndex(rec);
            if (mi >= matCount) continue;
            const uint8_t* mat = materials + static_cast<size_t>(mi) * off::kMomtStride;
            if (NativeShaderFor(Rd32(mat + off::kOffMomtShader)) != 6) continue;
            if (Rd32(mat + off::kOffMomtTexture2) != 0 || Rd32(mat + off::kOffMomtHandle2) != 0)
                return true; // shader-6 family WITH a second texture -> stock keeps it two-layer
        }
        return false;
    }

    /// Tag-driven group fill, merging the stock mandatory and optional walkers into one pass.
    bool WalkGroupModern(void* group, uint8_t* cursor)
    {
        auto* base = static_cast<uint8_t*>(GetPtr(group, off::kOffGroupBuffer));
        const uint32_t size = Rd32(Field(group, off::kOffGroupSize));
        if (!base || !cursor || size < 0x58)
            return false;

        // The sub-chunks live inside the MOGP payload; its size sits at buffer+0x10 (MVER is 0x0C,
        // then the MOGP tag at +0x0C and its size at +0x10). Clamp to the file for safety.
        uint8_t* end = base + size;
        const uint32_t mogpSize = Rd32(base + 0x10);
        if (mogpSize && mogpSize <= size - 0x14 && base + 0x14 + mogpSize < end)
            end = base + 0x14 + mogpSize;
        if (cursor >= end)
            return false;

        for (const auto& slot : off::kGroupSlots)
        {
            SetPtr(group, slot.ptrField, nullptr);
            if (slot.countField) SetU32(group, slot.countField, 0);
        }
        SetPtr(group, off::kGroupSlotMotv2.ptrField, nullptr);
        SetU32(group, off::kGroupSlotMotv2.countField, 0);
        SetPtr(group, off::kGroupSlotMocv2.ptrField, nullptr);
        SetU32(group, off::kGroupSlotMocv2.countField, 0);
        SetPtr(group, off::kOffGroupMorb, nullptr);

        uint8_t* mobn = nullptr; uint32_t mobnSize = 0;
        uint8_t* mobr = nullptr; uint32_t mobrSize = 0;
        uint32_t motvSeen = 0, mocvSeen = 0, unknown = 0;
        bool hasMocv = false;
        uint8_t* motv0 = nullptr; uint32_t motv0Size = 0; // captured for the UV-orientation probe
        uint8_t* motv1 = nullptr; uint32_t motv1Size = 0;

        auto store = [&](const off::ChunkSlot& slot, uint8_t* content, uint32_t chunkSize) {
            SetPtr(group, slot.ptrField, content);
            if (slot.countField)
                SetU32(group, slot.countField, slot.stride == 1 ? chunkSize : chunkSize / slot.stride);
        };

        WalkChunks(cursor, end, [&](uint32_t tag, uint8_t* content, uint32_t chunkSize) {
            // A modern group commonly ships TWO UV sets and sometimes THREE (202 and 7 of the 235 real
            // groups here). The client has slots for two; a third would otherwise overwrite the second,
            // so later sets are left in the buffer and counted instead.
            if (tag == off::WmoTag("MOTV"))
            {
                if (motvSeen == 0)      { store(off::kGroupSlots[4], content, chunkSize); motv0 = content; motv0Size = chunkSize; }
                else if (motvSeen == 1) { store(off::kGroupSlotMotv2, content, chunkSize); motv1 = content; motv1Size = chunkSize; }
                else                    ++unknown;
                ++motvSeen;
                return;
            }
            if (tag == off::WmoTag("MOCV"))
            {
                if (mocvSeen == 0)      { store(off::kGroupSlots[8], content, chunkSize); hasMocv = true; }
                else if (mocvSeen == 1) store(off::kGroupSlotMocv2, content, chunkSize);
                else                    ++unknown;
                ++mocvSeen;
                return;
            }
            if (tag == off::WmoTag("MOBN")) { mobn = content; mobnSize = chunkSize; return; }
            if (tag == off::WmoTag("MOBR")) { mobr = content; mobrSize = chunkSize; return; }
            if (tag == off::WmoTag("MORB")) { SetPtr(group, off::kOffGroupMorb, content); return; }
            if (tag == off::WmoTag("MLIQ")) { g_parkedLiquids.fetch_add(1, std::memory_order_relaxed); return; }
            if (tag == off::WmoTag("MOVX")) { g_parkedIndex32.fetch_add(1, std::memory_order_relaxed); return; }

            for (const auto& slot : off::kGroupSlots)
            {
                if (slot.tag != tag) continue;
                store(slot, content, chunkSize);
                return;
            }
            ++unknown;
        });
        g_unknownChunks.fetch_add(unknown, std::memory_order_relaxed);

        // EXPERIMENTAL UV-orientation probe (see g_uvMode). Rewrites the base UV set in place, or swaps
        // which set feeds the base texture, so the transform that de-rotates modern textures can be found
        // in game. Applied on the file buffer (the client's own working memory, like the MOCV rewrite).
        if (const int mode = g_uvMode.load(std::memory_order_relaxed); mode != 0 && motv0)
        {
            auto xform = [](uint8_t* uv, uint32_t bytes, int m) {
                for (uint32_t i = 0; i + 8 <= bytes; i += 8)
                {
                    float u, v; std::memcpy(&u, uv + i, 4); std::memcpy(&v, uv + i + 4, 4);
                    float nu = u, nv = v;
                    switch (m)
                    {
                        case 1: nu = v;  nv = u;  break; // swap u/v (reflect across the diagonal)
                        case 2: nu = v;  nv = -u; break; // rotate +90
                        case 3: nu = -v; nv = u;  break; // rotate -90
                        case 4: nu = u;  nv = -v; break; // flip v
                        case 5: nu = -u; nv = v;  break; // flip u
                        default: break;
                    }
                    std::memcpy(uv + i, &nu, 4); std::memcpy(uv + i + 4, &nv, 4);
                }
            };
            if (mode >= 1 && mode <= 5)
                xform(motv0, motv0Size, mode);
            else if (mode == 6 && motv1) // feed the SECOND UV set to the base texture (t0)
            {
                store(off::kGroupSlots[4], motv1, motv1Size);
                store(off::kGroupSlotMotv2, motv0, motv0Size);
            }
            g_uvTransformed.fetch_add(1, std::memory_order_relaxed);
        }

        // UV-set orientation, split by group kind so a single global choice never fights itself (the old
        // whack-a-mole). Legion's rule: a single-layer material samples the group's LAST UV set, a two-layer
        // Composite samples set0 (base) + set1 (overlay). Three cases, decided per group at load:
        //
        //   * 1-UV group (no motv1) -- base already samples the only set. Nothing to do.
        //   * 2+-set group with NO Composite batch -- stock builds it ONE-UV (it only flips to two-UV for a
        //     shader-6 batch), so its base texcoord slot (+0xF0) feeds the vertex buffer. Point that slot at
        //     the LAST set (motv1) so every single-layer batch samples set1, exactly as uv_mode 6 does but
        //     scoped to the groups that want it -- no shader/vertex-declaration dependency, it is pure data.
        //   * 2+-set group WITH a Composite batch -- stock builds it TWO-UV (both sets on every vertex), so
        //     Composite's base=set0/overlay=set1 are already right. Leave the slots; the per-batch VS route
        //     (CompositeShader) moves this group's single-layer batches onto set1 at draw, where TEXCOORD1
        //     genuinely exists in the declaration. Detecting Composite here reproduces the stock decision.
        if (motv1 && !GroupHasComposite(group))
        {
            store(off::kGroupSlots[4], motv1, motv1Size);   // base texcoord slot <- group's last UV set
            store(off::kGroupSlotMotv2, motv0, motv0Size);  // keep set0 in the 2nd slot (unread when 1-UV)
            g_baseSetReoriented.fetch_add(1, std::memory_order_relaxed);
        }

        // BSP: the stock walker hands the two adjacent chunks straight to CAaBsp, which only copies
        // pointers and counts. Both must be present -- a lone MOBN would leave the face list null.
        if (mobn && mobr)
        {
            wxl::game::Native<off::Wmo_BspInitFn>(off::kBspInit)(
                Field(group, off::kOffGroupBsp), nullptr,
                mobn, mobnSize >> 4, mobr, mobrSize >> 1,
                reinterpret_cast<float*>(Field(group, 0x34)));
        }

        // A modern group can carry geometry with NO texture coordinates at all: 24 of the 235 real
        // groups in this corpus are collision-only shells (a handful of faces, no MOBA, every batch
        // count 0). 3.3.5 has no such case, so CMapObjGroup::FillVertexVB0 reads MOTV (+0xF0) and MONR
        // (+0xEC) UNCONDITIONALLY as soon as the vertex count is non-zero, and faults on the null.
        // Such a group has nothing to draw, so only the runtime VERTEX count is zeroed: the vertex fill
        // skips it entirely, while MOPY / MOVI / MOVT and the BSP keep their own pointers and counts,
        // so collision and portals are untouched. A group that DOES have batches but no UVs would be a
        // different problem -- it gets an error line rather than silent parking.
        if (Rd32(Field(group, 0x15C)) != 0 && (!GetPtr(group, 0xF0) || !GetPtr(group, 0xEC)))
        {
            if (Rd32(Field(group, 0x16C)) != 0)
                WLOG_ERROR("wmo-native: group has %u batches but no MOTV/MONR; parking its vertices",
                           Rd32(Field(group, 0x16C)));
            SetU32(group, 0x15C, 0);
            g_parkedNoUv.fetch_add(1, std::memory_order_relaxed);
        }

        // MOBA material index: see config.hpp kNativeWmoMobaMaterialId for why this is a copy rather
        // than a read-site patch. Guarded three ways -- the batch must announce the modern form, the
        // destination byte must still be untouched, and the index must be one this client can address
        // (its readers are all `movzx byte`, so anything above 255 is unreachable by construction).
        if constexpr (wxl::features::kNativeWmoMobaMaterialId)
        {
            auto* batches = static_cast<uint8_t*>(GetPtr(group, off::kGroupSlots[5].ptrField));
            const uint32_t batchCount = Rd32(Field(group, off::kGroupSlots[5].countField));
            if (batches)
            {
                uint32_t moved = 0;
                for (uint32_t i = 0; i < batchCount; ++i)
                {
                    uint8_t* rec = batches + i * off::kMobaStride;
                    if (!(rec[off::kOffMobaFlags] & off::kMobaFlagMaterialModern)) continue;
                    if (rec[off::kOffMobaMaterial] != 0) continue;
                    uint16_t modern;
                    std::memcpy(&modern, rec + off::kOffMobaMaterialModern, 2);
                    if (modern > 0xFF) { g_materialOutOfRange.fetch_add(1, std::memory_order_relaxed); continue; }
                    rec[off::kOffMobaMaterial] = static_cast<uint8_t>(modern);
                    ++moved;
                }
                g_materialIdsMoved.fetch_add(moved, std::memory_order_relaxed);
            }
        }

        // Vertex-colour alpha rewrite. The stock gate skips it when MOHD 0x08 (do_not_fix_vertex_color
        // _alpha) is set -- but on this corpus EVERY modern root sets 0x08, because it means "the unified
        // shader folds the fix in, don't do it on the CPU". 3.3.5 has no unified shader: it MULTIPLIES
        // MOCV into the texture, and the modern MOCV is near-black (mean RGB ~20/255, baked light stored
        // additively), so honouring 0x08 renders every surface black. So for a modern group we run the
        // stock CPU fix anyway (g_forceVertexColorFix, default on) -- adapting the client to the fact that,
        // lacking the shader the flag assumes, it must keep doing the fix the flag tries to disable.
        if (hasMocv)
        {
            // Near-white placeholder neutralization (ex-hot-convert parity). A modern source leaves some
            // exterior groups' MOCV at near-white because its shader path needs no baked lighting; the 3.3.5
            // client MULTIPLIES the surface by the vertex colour, so a near-white group renders almost white.
            // Zero the RGB of any near-white entry (>=220 on all of B,G,R) -- it then shades from the
            // ambient/scene light like the correctly-baked (darker) groups. Alpha and darker colours are left
            // as-is. Runs BEFORE the client's alpha fixup below, exactly where the converter did it (on the
            // raw MOCV). MOCV is BGRA, stride 4; the count slot holds MOCV_size/4.
            if (g_neutralizeNearWhite.load(std::memory_order_relaxed))
            {
                auto* mocv = static_cast<uint8_t*>(GetPtr(group, off::kGroupSlots[8].ptrField));
                const uint32_t mocvCount = Rd32(Field(group, off::kGroupSlots[8].countField));
                if (mocv)
                {
                    uint32_t zeroed = 0;
                    for (uint32_t v = 0; v < mocvCount; ++v)
                    {
                        uint8_t* e = mocv + static_cast<size_t>(v) * 4u; // B,G,R,A
                        if (e[0] >= 220 && e[1] >= 220 && e[2] >= 220)
                        {
                            e[0] = 0; e[1] = 0; e[2] = 0;
                            ++zeroed;
                        }
                    }
                    if (zeroed) g_mocvNeutralized.fetch_add(zeroed, std::memory_order_relaxed);
                }
            }

            void* root = GetPtr(group, off::kOffGroupRoot);
            auto* mohd = root ? static_cast<uint8_t*>(GetPtr(root, off::kOffMohd)) : nullptr;
            const uint32_t mohdFlags = mohd ? Rd32(mohd + off::kOffMohdFlags) : 0;
            const bool stockWantsFix = !(mohdFlags & off::kMohdFlagSkipColorFix);
            if (g_forceVertexColorFix.load(std::memory_order_relaxed) || stockWantsFix)
            {
                wxl::game::Native<off::Wmo_FixColorVertexAlphaFn>(off::kFixColorVertexAlpha)(group, nullptr);
                g_vertexColorFixed.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return true;
    }

    void __fastcall hkGroupWalk(void* group, void* edx, void* cursor)
    {
        void* root = GetPtr(group, off::kOffGroupRoot);
        if (!root || !RootIsModern(root))
        {
            g_origGroupWalk(group, edx, cursor);
            return;
        }

        bool ok = false;
        __try { ok = WalkGroupModern(group, static_cast<uint8_t*>(cursor)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }

        if (ok) g_groupsModern.fetch_add(1, std::memory_order_relaxed);
        else    g_groupsFailed.fetch_add(1, std::memory_order_relaxed);
    }

    // ------------------------------------------------------------------ materials
    /**
     * @brief Maps a modern WMO pixel-shader id onto the nearest native 3.3.5 effect (0..6).
     *
     * 3.3.5 ships effects 0..6 (Diffuse, Specular, Metal, Env, Opaque, EnvMetal, TwoLayerDiffuse) and its
     * effect lookup is UNCHECKED, so a modern id selects past the table and the next SetShaders faults.
     * The modern enum is a superset built from the SAME families, and on this corpus every material is
     * blend 0 (opaque), so a family remap keeps far more than the old collapse-to-0 did:
     *   - two-layer families (7 TwoLayerEnvMetal, 13 TwoLayerDiffuseOpaque, 15/18/19/20, 21) -> 6
     *     TwoLayerDiffuse: the second diffuse layer, blended by vertex-colour alpha exactly as native 6
     *     does, is preserved (texture_2 is already resolved below). The env/mod2x refinement is dropped.
     *   - env families (11 MaskedEnvMetal, 12 EnvMetalEmissive, 17) -> 5 EnvMetal: base + env map kept,
     *     emissive term dropped.
     *   - single-layer families (9 DiffuseEmissive, 10, 14, 16) -> 0 Diffuse.
     * The dropped terms (environment reflection, emissive glow, mod2x) are the only fidelity gap, and
     * they are the custom-shader follow-up. Native ids (<= 6) pass through untouched.
     * @param modern  MOMT shader id straight from the file.
     * @return A shader id in 0..6 the client has an effect for.
     */
    inline uint32_t NativeShaderFor(uint32_t modern)
    {
        if (modern <= off::kMaxClientShaderId) return modern; // already a native effect
        // Classification taken from the Legion shader table (RE'd from the 7.3.5 Ghidra export): the
        // vertex shader per id decides the layer count and texcoord routing. Map each modern id onto the
        // nearest native effect of the SAME layer family so texture_1 keeps set 0 and a genuine second
        // diffuse layer keeps set 1. Getting the family right matters: id 21 is MapObjLod (SINGLE layer),
        // not two-layer -- routing it through Composite would blend a second texture the shader ignores.
        switch (modern)
        {
            // two-layer diffuse (tex1->set0, tex2->set1): 7 TwoLayerEnvMetal, 9 DiffuseEmissive,
            // 13 TwoLayerDiffuseOpaque, 15 TwoLayerDiffuseEmissive, 18 Mod2x, 19 Mod2xNA, 20 Alpha.
            case 7: case 9: case 13: case 15: case 18: case 19: case 20:
                return 6;
            // env-metal family (tex1->set0 + generated env): 11 MaskedEnvMetal, 12 EnvMetalEmissive,
            // 17 AdditiveMaskedEnvMetal. Their extra set-1 layer is dropped (native 5 cannot express it).
            case 11: case 12: case 17:
                return 5;
            // single-layer (tex1->set0 only): 8 TwoLayerTerrain (2nd coord is vertex-generated, not a MOTV
            // set), 16 Diffuse, 21 MapObjLod, 10/14, and anything unforeseen.
            default:
                return 0;
        }
    }

    /**
     * @brief Modern replacement for CMapObj::CreateMaterial.
     *
     * Reproduces the stock contract field for field -- idempotent guard on the first handle, the
     * fallback name for an empty texture_1, the shader 3/5/6 -> 4 collapse when texture_2 is empty, and
     * the second texture dropped when the shader pipeline is off -- but sources both names from the
     * FileDataID service instead of a MOTX blob the modern root does not have.
     */
    void __fastcall hkCreateMaterial(void* root, void* edx, int materialIndex)
    {
        if (!RootIsModern(root))
        {
            g_origCreateMaterial(root, edx, materialIndex);
            return;
        }

        auto* materials = static_cast<uint8_t*>(GetPtr(root, off::kOffMaterialBase));
        const uint32_t count = Rd32(Field(root, off::kOffMaterialCount));
        if (!materials || materialIndex < 0 || static_cast<uint32_t>(materialIndex) >= count)
            return;

        uint8_t* record = materials + static_cast<size_t>(materialIndex) * off::kMomtStride;
        if (Rd32(record + off::kOffMomtHandle1) != 0)
            return;                                     // already resolved (stock guard)

        auto resolve = [](uint32_t fileDataId) -> const char* {
            if (!fileDataId) return nullptr;
            const char* path = wxl::fdid::ResolveTexture(fileDataId);
            if (path && path[0]) { g_texResolved.fetch_add(1, std::memory_order_relaxed); return path; }
            g_texUnresolved.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        };

        const char* first  = resolve(Rd32(record + off::kOffMomtTexture1));
        const char* second = resolve(Rd32(record + off::kOffMomtTexture2));
        if (!first || !first[0])
            first = off::kFallbackTextureName;
        if (Rd32(reinterpret_cast<void*>(off::kShaderEffectsEnabled)) == 0)
            second = nullptr;

        // Shader id: 3.3.5 only has effects 0..6, and its shader-effect lookup is UNCHECKED. A modern
        // id (13, 7, 21, 12, ... on this corpus) selects past the effect table, the render path calls
        // CShaderEffect::SetCurrent with a null effect, and the next SetShaders faults dereferencing
        // `DAT_00D43024 + 0x2C + index*4` at a near-null address.
        //
        // Remap it onto the nearest native effect by combiner family (NativeShaderFor) rather than
        // flattening everything to 0: the two-layer families keep their second diffuse layer through
        // native effect 6, so ~86% of this corpus renders with the blend the file asked for. The env
        // reflection / emissive glow that native 5/6 cannot express is the only loss, and it is the
        // custom-shader follow-up. The rewrite lands on the client's own working buffer, exactly like
        // the 3/5/6 -> 4 rewrite below. Per-family counters make the trade visible in game.
        const uint32_t sourceShader = Rd32(record + off::kOffMomtShader);
        const uint32_t nativeShader =
            g_shaderRemapEnabled.load(std::memory_order_relaxed)
                ? NativeShaderFor(sourceShader)                                   // family remap (default)
                : (sourceShader > off::kMaxClientShaderId ? 0u : sourceShader);   // old single-layer fallback
        if (nativeShader != sourceShader)
        {
            SetU32(record, off::kOffMomtShader, nativeShader);
            g_shaderRemapped.fetch_add(1, std::memory_order_relaxed);
            switch (nativeShader)
            {
                case 6:  g_shaderToTwoLayer.fetch_add(1, std::memory_order_relaxed); break;
                case 5:  g_shaderToEnv.fetch_add(1, std::memory_order_relaxed);      break;
                default: g_shaderToSingle.fetch_add(1, std::memory_order_relaxed);   break;
            }
            const uint32_t bit = 1u << (sourceShader & 31);
            if ((g_shaderSeen.fetch_or(bit, std::memory_order_relaxed) & bit) == 0)
                WLOG_INFO("wmo-native: modern shader %u -> native effect %u (%s)", sourceShader,
                          nativeShader,
                          nativeShader == 6 ? "two-layer diffuse"
                                            : nativeShader == 5 ? "env-metal" : "diffuse");
        }

        switch (Rd32(record + off::kOffMomtShader))
        {
            case 3: case 5: case 6:
                if (second && second[0]) break;         // keeps its second texture, shader unchanged
                SetU32(record, off::kOffMomtShader, 4);
                [[fallthrough]];
            case 0: case 1: case 2: case 4:
                second = nullptr;
                break;
            default:
                break;
        }

        auto load = wxl::game::Native<adt::Map_LoadTextureFn>(adt::kMapLoadTexture);
        Wr32(record + off::kOffMomtHandle1,
             static_cast<uint32_t>(reinterpret_cast<uintptr_t>(load(first))));
        Wr32(record + off::kOffMomtHandle2,
             second && second[0]
                 ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(load(second)))
                 : 0u);
    }

    // ------------------------------------------------------------------ per-batch cull
    off::Wmo_CullBatchFn g_origCullBatch = nullptr;

    /**
     * @brief Per-batch AABB cull, bypassed for modern batches.
     *
     * The stock test reads six i16 at record+0x00. A modern record has zeros there except +0x0A, where
     * the material index lives -- so every modern batch claims a ZERO-VOLUME box at the group origin
     * and is rejected as soon as the camera comes close enough for that single point to leave the
     * frustum. That is exactly the "the nearer I get, the more of the building disappears" symptom.
     *
     * A modern file simply does not ship this box (Legion culls per group and on the GPU). Inventing
     * one inside the record is not an option -- it would overwrite the material index that legitimately
     * lives at +0x0A -- so the per-batch REFINEMENT is skipped for modern batches: they are reported
     * visible, and group-level frustum, portal and horizon culling still decide. The cost is a few
     * extra batches submitted, for modern WMOs only. A real box, derived from MOVT over each batch's
     * vertex range and held in a side table, is the follow-up.
     *
     * The modern test is deliberately narrow: the announce bit AND an all-zero min/max.xy. A stock
     * batch would have to carry both to be mistaken for modern, and it would only lose its per-batch
     * refinement -- never render wrongly.
     */
    char __cdecl hkCullBatch(void* mobaRecord)
    {
        if (mobaRecord)
        {
            const auto* rec = static_cast<const uint8_t*>(mobaRecord);
            if ((rec[off::kOffMobaFlags] & off::kMobaFlagMaterialModern) != 0 &&
                Rd32(rec) == 0 && Rd32(rec + 4) == 0 && rec[8] == 0 && rec[9] == 0)
            {
                g_batchCullBypassed.fetch_add(1, std::memory_order_relaxed);
                return 0;   // 0 = keep this batch
            }
        }
        return g_origCullBatch(mobaRecord);
    }

    // ------------------------------------------------------------------ install
    bool InstallWmoNative()
    {
        const bool rootOk = wxl::hook::Install("WmoNative_RootWalk", off::kRootWalk,
                                               &hkRootWalk, &g_origRootWalk);
        const bool groupOk = wxl::hook::Install("WmoNative_GroupWalk", off::kGroupWalk,
                                                &hkGroupWalk, &g_origGroupWalk);
        const bool matOk = wxl::hook::Install("WmoNative_CreateMaterial", off::kResolveMaterialTexture,
                                              &hkCreateMaterial, &g_origCreateMaterial);
        const bool cullOk = wxl::hook::Install("WmoNative_CullBatch", off::kCullBatch,
                                               &hkCullBatch, &g_origCullBatch);

        // All or nothing: a half-installed reader would classify roots as modern and then hand them to
        // a walker that never ran, which is the one failure mode worse than staying stock.
        const bool ok = rootOk && groupOk && matOk && cullOk;
        g_installed.store(ok, std::memory_order_relaxed);
        if (!ok)
            WLOG_ERROR("wmo-native: install failed (root=%d group=%d material=%d cull=%d), staying stock",
                       rootOk ? 1 : 0, groupOk ? 1 : 0, matOk ? 1 : 0, cullOk ? 1 : 0);
        return ok;
    }
}

// ---------------------------------------------------------------- public query surface
namespace wxl::runtime::wmonative
{
    Stats GetStats()
    {
        Stats s{};
        s.rootsModern        = g_rootsModern.load(std::memory_order_relaxed);
        s.rootsStock         = g_rootsStock.load(std::memory_order_relaxed);
        s.groupsModern       = g_groupsModern.load(std::memory_order_relaxed);
        s.groupsFailed       = g_groupsFailed.load(std::memory_order_relaxed);
        s.texturesResolved   = g_texResolved.load(std::memory_order_relaxed);
        s.texturesUnresolved = g_texUnresolved.load(std::memory_order_relaxed);
        s.parkedDoodadDefs   = g_parkedDoodads.load(std::memory_order_relaxed);
        s.parkedLiquids      = g_parkedLiquids.load(std::memory_order_relaxed);
        s.parkedIndex32      = g_parkedIndex32.load(std::memory_order_relaxed);
        s.unknownChunks      = g_unknownChunks.load(std::memory_order_relaxed);
        s.shaderRemapped     = g_shaderRemapped.load(std::memory_order_relaxed);
        s.shaderToTwoLayer   = g_shaderToTwoLayer.load(std::memory_order_relaxed);
        s.shaderToEnv        = g_shaderToEnv.load(std::memory_order_relaxed);
        s.shaderToSingle     = g_shaderToSingle.load(std::memory_order_relaxed);
        s.parkedNoUvGroups   = g_parkedNoUv.load(std::memory_order_relaxed);
        s.materialIdsMoved   = g_materialIdsMoved.load(std::memory_order_relaxed);
        s.materialIdsOutOfRange = g_materialOutOfRange.load(std::memory_order_relaxed);
        s.batchCullBypassed  = g_batchCullBypassed.load(std::memory_order_relaxed);
        s.vertexColorFixed   = g_vertexColorFixed.load(std::memory_order_relaxed);
        s.mocvNeutralized    = g_mocvNeutralized.load(std::memory_order_relaxed);
        s.uvTransformed      = g_uvTransformed.load(std::memory_order_relaxed);
        s.baseSetReoriented  = g_baseSetReoriented.load(std::memory_order_relaxed);
        return s;
    }

    bool Enabled() { return wxl::features::kNativeWmo; }

    bool Installed() { return g_installed.load(std::memory_order_relaxed); }

    bool IsModernRoot(void* root) { return root && RootIsModern(root); }

    bool ShaderRemapEnabled() { return g_shaderRemapEnabled.load(std::memory_order_relaxed); }
    void SetShaderRemapEnabled(bool on) { g_shaderRemapEnabled.store(on, std::memory_order_relaxed); }

    bool VertexColorFixEnabled() { return g_forceVertexColorFix.load(std::memory_order_relaxed); }
    void SetVertexColorFixEnabled(bool on) { g_forceVertexColorFix.store(on, std::memory_order_relaxed); }

    bool NearWhiteNeutralizeEnabled() { return g_neutralizeNearWhite.load(std::memory_order_relaxed); }
    void SetNearWhiteNeutralizeEnabled(bool on) { g_neutralizeNearWhite.store(on, std::memory_order_relaxed); }

    int  UvMode() { return g_uvMode.load(std::memory_order_relaxed); }
    void SetUvMode(int mode) { g_uvMode.store(mode, std::memory_order_relaxed); }
}

WXL_REGISTER_FEATURE("wmo-native", wxl::features::kNativeWmo, InstallWmoNative)
