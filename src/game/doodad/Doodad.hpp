// doodad bindings: enumerate placed map doodads near the player and read / move their transform.
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
#include <cstddef>
#include <windows.h>

#include "game/Binding.hpp"
#include "offsets/game/ADT.hpp"
#include "offsets/game/Doodad.hpp"

/**
 * @brief Walks the placed-doodad list of the player's current chunk and reads or moves a doodad's world placement.
 *
 * Pointer reads are range-guarded and the walk is bounded, so a stale or odd list degrades to a short list
 * instead of a crash.
 */
namespace wxl::game::doodad
{
    namespace off  = wxl::offsets::game::doodad;
    namespace aoff = wxl::offsets::game::adt;

    namespace detail
    {
        /**
         * @brief Reports coarse user-space pointer sanity for a 32-bit fixed-base image.
         * @param p  Pointer to test.
         * @return True when the address falls in the plausible user range.
         */
        inline bool Plausible(const void* p)
        {
            const uintptr_t a = reinterpret_cast<uintptr_t>(p);
            return a > 0x10000 && a < 0x7FFF0000;
        }

        /**
         * @brief Per-thread cache of the last few committed regions VirtualQuery returned.
         *
         * The guarded walkers (ModelName, EnumerateChunk, ...) probe the same handful of regions
         * hundreds of times per call — up to once per character or four times per list node — and
         * each probe was a kernel round-trip. Entries only live within a single GetTickCount64
         * millisecond, so a page freed by another thread is trusted no longer than roughly the
         * check-then-read window the uncached code already had.
         */
        struct RegionCache
        {
            struct Entry { uintptr_t base = 0; uintptr_t end = 0; DWORD protect = 0; };
            static constexpr size_t kEntries = 4;
            Entry    e[kEntries];
            size_t   next  = 0;
            uint64_t stamp = 0;
        };
        inline RegionCache& Cache() { thread_local RegionCache c; return c; }

        /**
         * @brief Reports whether [p, p+n) is committed and accessible with one of the given page protections.
         * @param p      Start address.
         * @param n      Byte count.
         * @param allow  Bitmask of accepted page protection flags.
         * @return True when the range is committed and matches an allowed protection.
         */
        inline bool Accessible(const void* p, size_t n, DWORD allow)
        {
            if (!Plausible(p)) return false;
            const uintptr_t a = reinterpret_cast<uintptr_t>(p);

            RegionCache& c = Cache();
            const uint64_t now = GetTickCount64();
            if (c.stamp != now) { c = RegionCache{}; c.stamp = now; }
            for (const RegionCache::Entry& hit : c.e)
                if (hit.end && a >= hit.base && a + n <= hit.end)
                    return (hit.protect & allow) != 0;

            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(p, &mbi, sizeof mbi) == 0) return false;
            if (mbi.State != MEM_COMMIT) return false;
            if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
            const uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            c.e[c.next] = { reinterpret_cast<uintptr_t>(mbi.BaseAddress), end, mbi.Protect };
            c.next = (c.next + 1) % RegionCache::kEntries;
            if ((mbi.Protect & allow) == 0) return false;
            return a + n <= end;
        }
        /**
         * @brief Reports whether [p, p+n) is readable.
         * @param p  Start address.
         * @param n  Byte count.
         * @return True when the range is committed and readable.
         */
        inline bool Readable(const void* p, size_t n)
        {
            return Accessible(p, n, PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
        }
        /**
         * @brief Reports whether [p, p+n) is writable.
         * @param p  Start address.
         * @param n  Byte count.
         * @return True when the range is committed and writable.
         */
        inline bool Writable(const void* p, size_t n)
        {
            return Accessible(p, n, PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
        }

        /** @brief Returns a float pointer at offset o within d. */
        inline float* F(void* d, size_t o) { return reinterpret_cast<float*>(reinterpret_cast<char*>(d) + o); }
        /**
         * @brief Reads a pointer slot at offset o within d, guarded.
         * @param d  Base object.
         * @param o  Byte offset of the slot.
         * @return The slot value, or null if the slot is not mapped.
         */
        inline void* P(void* d, size_t o)
        {
            void* slot = reinterpret_cast<char*>(d) + o;
            return Readable(slot, sizeof(void*)) ? *reinterpret_cast<void**>(slot) : nullptr;
        }

        /**
         * @brief Reports whether a value is a finite world coordinate within the map bounds.
         * @param v  Coordinate to test.
         * @return True for a finite in-bounds value (self-compare rejects NaN).
         */
        inline bool SaneCoord(float v) { return v == v && v > -64000.0f && v < 64000.0f; }
        /**
         * @brief Reports whether a candidate points at a doodad with a mapped, sane position block.
         * @param d  Candidate doodad pointer.
         * @return True when the position block is mapped and reads as a sane location.
         */
        inline bool SaneDoodad(void* d)
        {
            return Readable(d, off::kPosZ + sizeof(float)) && SaneCoord(*F(d, off::kPosX))
                && SaneCoord(*F(d, off::kPosY)) && SaneCoord(*F(d, off::kPosZ));
        }
    }

    /**
     * @brief Reports whether a doodad is still live (position block mapped and a sane world location).
     * @param d  Doodad pointer.
     * @return True when the doodad reads as valid.
     */
    inline bool IsValid(void* d) { return detail::SaneDoodad(d); }

    /**
     * @brief Reads the render instance the doodad draws through (doodad+0x34).
     * @param d  Doodad pointer.
     * @return The render instance, or null while the model is still loading. The live world matrix the
     *         renderer reads each frame lives on this instance, not the doodad.
     */
    inline void* Instance(void* d)
    {
        if (!detail::Readable(d, off::kInstance + sizeof(void*))) return nullptr;
        return static_cast<off::MapDoodad*>(d)->instance;
    }

    /**
     * @brief Copies the model file name of a placed doodad into out.
     *
     * Walks instance(+0x34) -> model(+0x2c) -> inline path(+0x3c) and copies the bare file name, or the
     * full path when no separator is present.
     * @param d    Doodad pointer.
     * @param out  Destination buffer.
     * @param cap  Capacity of out in bytes.
     * @return True on success, false when not yet loaded or on a bad buffer.
     */
    inline bool ModelName(void* d, char* out, size_t cap)
    {
        if (!out || cap == 0) return false;
        out[0] = '\0';
        void* inst = Instance(d);
        if (!inst) return false;
        void* model = detail::P(inst, off::kInstModel);
        if (!model) return false;
        const char* path = static_cast<off::M2ModelCache*>(model)->fullPath;
        if (!detail::Readable(path, 1)) return false;
        // Find the last path separator so we show just the file name.
        const char* name = path;
        for (const char* s = path; detail::Readable(s, 1) && *s && (s - path) < 512; ++s)
            if (*s == '\\' || *s == '/') name = s + 1;
        size_t i = 0;
        for (; i + 1 < cap && detail::Readable(name + i, 1) && name[i]; ++i) out[i] = name[i];
        out[i] = '\0';
        return i > 0;
    }

    /**
     * @brief Reads the runtime chunk under a world position.
     * @param pos  World-space position in pos[0..2].
     * @return The chunk, or null.
     */
    inline void* ChunkAt(float pos[3]) { return Native<aoff::Map_GetChunkFn>(aoff::kGetChunk)(pos); }

    /**
     * @brief Appends one chunk's placed doodads to out[] starting at index n.
     *
     * Walks the intrusive list (head @ +0xCC, next @ node + linkOff + 4) with every read guarded, so a
     * bad list stops instead of crashing. Only placed map doodads are in this list, so creatures never appear.
     * @param chunk     Chunk to enumerate.
     * @param out       Destination array.
     * @param n         Starting index into out.
     * @param maxCount  Capacity of out.
     * @return The new count.
     */
    inline int EnumerateChunk(void* chunk, void** out, int n, int maxCount)
    {
        if (!detail::Readable(chunk, off::kChunkDoodadHead + sizeof(void*))) return n;
        const uint32_t linkOff =
            *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(chunk) + off::kChunkDoodadLinkOff);
        if (linkOff > 0x400) return n; // a real link offset is small; reject a garbage field
        uintptr_t node = reinterpret_cast<uintptr_t>(detail::P(chunk, off::kChunkDoodadHead));
        for (int i = 0; (node & 1) == 0 && node != 0 && i < 8192 && n < maxCount; ++i)
        {
            void* d = detail::P(reinterpret_cast<void*>(node), off::kNodeDoodad);
            if (detail::SaneDoodad(d)) out[n++] = d;
            node = reinterpret_cast<uintptr_t>(detail::P(reinterpret_cast<void*>(node), linkOff + 4));
        }
        return n;
    }

    /**
     * @brief Collects the doodads of the chunk under a single world position.
     * @param pos       World-space position in pos[0..2].
     * @param out       Destination array.
     * @param maxCount  Capacity of out.
     * @return The doodad count.
     */
    inline int EnumerateAt(float pos[3], void** out, int maxCount)
    {
        return EnumerateChunk(ChunkAt(pos), out, 0, maxCount);
    }

    /**
     * @brief Collects the doodads of the 3x3 grid of chunks around a center.
     *
     * Enumerating the neighbouring chunks lets a cursor pick find doodads near the edges.
     * @param center    World-space center in center[0..2].
     * @param step      Grid spacing, about one chunk (33.33 yd).
     * @param out       Destination array.
     * @param maxCount  Capacity of out.
     * @return The doodad count.
     */
    inline int EnumerateAround(float center[3], float step, void** out, int maxCount)
    {
        void* seen[16]; int seenN = 0, n = 0;
        for (int gy = -1; gy <= 1; ++gy)
            for (int gx = -1; gx <= 1; ++gx)
            {
                float q[3] = { center[0] + gx * step, center[1] + gy * step, center[2] };
                void* chunk = ChunkAt(q);
                if (!detail::Plausible(chunk)) continue;
                bool dup = false;
                for (int k = 0; k < seenN; ++k) if (seen[k] == chunk) dup = true;
                if (dup) continue;
                if (seenN < 16) seen[seenN++] = chunk;
                n = EnumerateChunk(chunk, out, n, maxCount);
                if (n >= maxCount) return n;
            }
        return n;
    }

    /**
     * @brief Reads a doodad's world position, or zeros when the block is not mapped.
     * @param d    Doodad pointer.
     * @param out  Receives the position in out[0..2].
     */
    inline void Position(void* d, float out[3])
    {
        if (!detail::Readable(d, off::kScale + sizeof(float))) { out[0] = out[1] = out[2] = 0.0f; return; }
        const auto* dd = static_cast<const off::MapDoodad*>(d);
        out[0] = dd->pos[0]; out[1] = dd->pos[1]; out[2] = dd->pos[2];
    }

    /**
     * @brief Reads a doodad's scale.
     * @param d  Doodad pointer.
     * @return The scale, or 1.0 when the field is not mapped.
     */
    inline float Scale(void* d)
    {
        return detail::Readable(d, off::kScale + sizeof(float)) ? static_cast<const off::MapDoodad*>(d)->scale : 1.0f;
    }

    /**
     * @brief Reads the cursor-pick target: the world bounding-sphere center.
     * @param d    Doodad pointer.
     * @param out  Receives the center in out[0..2], falling back to the position when that field reads
     *             unmapped or implausibly far from the position.
     */
    inline void Center(void* d, float out[3])
    {
        float pos[3];
        Position(d, pos);
        if (detail::Readable(d, off::kCenterZ + sizeof(float)))
        {
            const auto* dd = static_cast<const off::MapDoodad*>(d);
            const float c0 = dd->center[0];
            const float c1 = dd->center[1];
            const float c2 = dd->center[2];
            const float dx = c0 - pos[0], dy = c1 - pos[1], dz = c2 - pos[2];
            if (detail::SaneCoord(c0) && detail::SaneCoord(c1) && detail::SaneCoord(c2) &&
                dx * dx + dy * dy + dz * dz < 300.0f * 300.0f)
            {
                out[0] = c0; out[1] = c1; out[2] = c2; return;
            }
        }
        out[0] = pos[0]; out[1] = pos[1]; out[2] = pos[2];
    }

    /**
     * @brief Reads the doodad's world-space bounding box.
     * @param d   Doodad pointer.
     * @param mn  Receives the box minimum in mn[0..2].
     * @param mx  Receives the box maximum in mx[0..2].
     * @return True on success, false when the block is not mapped.
     */
    inline bool BBox(void* d, float mn[3], float mx[3])
    {
        if (!detail::Readable(d, off::kBBoxMaxZ + sizeof(float))) return false;
        const auto* dd = static_cast<const off::MapDoodad*>(d);
        mn[0] = dd->bboxMin[0]; mn[1] = dd->bboxMin[1]; mn[2] = dd->bboxMin[2];
        mx[0] = dd->bboxMax[0]; mx[1] = dd->bboxMax[1]; mx[2] = dd->bboxMax[2];
        return true;
    }

    /**
     * @brief Reads the model-LOCAL bounding box from the parsed MD20 header.
     *
     * Follows instance(+0x34) -> model cache(+0x2c) -> MD20 header(+0x150), then reads the local AABB at
     * H+0xa0 / H+0xac. Unlike the doodad's own bbox fields (a degenerate point set once at spawn) this is the
     * model's real extents; transform the 8 corners by WorldMatrix to get the world box of the placement.
     * @param d   Doodad pointer.
     * @param lo  Receives the local box minimum in lo[0..2].
     * @param hi  Receives the local box maximum in hi[0..2].
     * @return True on success, false when the model is still loading or the box reads degenerate or absurd.
     */
    inline bool LocalBounds(void* d, float lo[3], float hi[3])
    {
        void* inst = Instance(d);
        if (!inst) return false;
        void* model = detail::P(inst, off::kInstModel);
        if (!model) return false;
        void* hdr = detail::P(model, off::kModelHeader);
        if (!hdr) return false;
        if (!detail::Readable(hdr, sizeof(off::MD20Header))) return false;

        const auto* h = static_cast<const off::MD20Header*>(hdr);
        lo[0] = h->bboxMin[0]; lo[1] = h->bboxMin[1]; lo[2] = h->bboxMin[2];
        hi[0] = h->bboxMax[0]; hi[1] = h->bboxMax[1]; hi[2] = h->bboxMax[2];

        float span = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            if (lo[i] != lo[i] || hi[i] != hi[i]) return false; // NaN
            if (hi[i] < lo[i]) return false;                    // inverted
            const float e = hi[i] - lo[i];
            if (e > 100000.0f) return false;                    // absurd
            span += e;
        }
        return span > 1e-4f; // reject a degenerate point
    }

    /**
     * @brief Reads the live world matrix the renderer reads each frame: instance(+0x34) + 0xb4.
     * @param d  Doodad pointer.
     * @param m  Receives 16 floats, row-major D3D row-vector with translation in row 3 (m[12..14]).
     * @return True on success, false when the model is not loaded or not mapped.
     */
    inline bool WorldMatrix(void* d, float m[16])
    {
        void* inst = Instance(d);
        if (!inst) return false;
        const auto* in = static_cast<const off::M2Instance*>(inst);
        if (!detail::Readable(in->worldMatrix, 16 * sizeof(float))) return false;
        for (int i = 0; i < 16; ++i) m[i] = in->worldMatrix[i];
        return true;
    }

    /**
     * @brief Replaces the whole world transform from an edited matrix.
     *
     * The render-critical write is the instance matrix (+0xb4); the doodad-side staging copies
     * (+0xd8 / +0x6c) are mirrored so the editor's source of truth and any later save stay consistent.
     * Without the instance write nothing moves.
     * @param d  Doodad pointer.
     * @param m  Source matrix of 16 floats, row-major.
     */
    inline void SetWorldMatrix(void* d, const float m[16])
    {
        if (void* inst = Instance(d))
        {
            auto* in = static_cast<off::M2Instance*>(inst);
            if (detail::Writable(in->worldMatrix, 16 * sizeof(float)))
                for (int i = 0; i < 16; ++i) in->worldMatrix[i] = m[i];
        }

        auto* dd = static_cast<off::MapDoodad*>(d);
        if (detail::Writable(dd->worldMatrix, 16 * sizeof(float)))
            for (int i = 0; i < 16; ++i) dd->worldMatrix[i] = m[i];
        if (detail::Writable(dd->pos, 3 * sizeof(float)))
        {
            dd->pos[0] = m[12]; dd->pos[1] = m[13]; dd->pos[2] = m[14];
        }
    }

    /**
     * @brief Moves a doodad (translate only) by rewriting the translation row of the live instance matrix.
     *
     * The change is mirrored into the doodad staging fields. The instance write is what actually moves the model.
     * @param d  Doodad pointer.
     * @param p  New world position in p[0..2].
     */
    inline void SetPosition(void* d, const float p[3])
    {
        if (void* inst = Instance(d))
        {
            auto* in = static_cast<off::M2Instance*>(inst);
            if (detail::Writable(in->worldMatrix + 12, 3 * sizeof(float)))
            {
                in->worldMatrix[12] = p[0]; in->worldMatrix[13] = p[1]; in->worldMatrix[14] = p[2];
            }
        }
        auto* dd = static_cast<off::MapDoodad*>(d);
        if (detail::Writable(dd->pos, 3 * sizeof(float)))
        {
            dd->pos[0] = p[0]; dd->pos[1] = p[1]; dd->pos[2] = p[2];
        }
        if (detail::Writable(dd->worldMatrix + 12, 3 * sizeof(float)))
        {
            dd->worldMatrix[12] = p[0]; dd->worldMatrix[13] = p[1]; dd->worldMatrix[14] = p[2];
        }
    }

    /** @brief Adds the doodad bindings to the enumerable catalog. */
    inline void RegisterCatalog()
    {
        Register({ "Doodad::ChunkAt", aoff::kGetChunk, "void*(float pos[3]) - chunk holding doodads" });
    }
}
