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
         * @brief Reports whether [p, p+n) is committed and accessible with one of the given page protections.
         * @param p      Start address.
         * @param n      Byte count.
         * @param allow  Bitmask of accepted page protection flags.
         * @return True when the range is committed and matches an allowed protection.
         */
        inline bool Accessible(const void* p, size_t n, DWORD allow)
        {
            if (!Plausible(p)) return false;
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(p, &mbi, sizeof mbi) == 0) return false;
            if (mbi.State != MEM_COMMIT) return false;
            if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
            if ((mbi.Protect & allow) == 0) return false;
            const uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            return reinterpret_cast<uintptr_t>(p) + n <= end;
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
    inline void* Instance(void* d) { return detail::P(d, off::kInstance); }

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
        const char* path = reinterpret_cast<const char*>(model) + off::kModelFullPath;
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
        out[0] = *detail::F(d, off::kPosX);
        out[1] = *detail::F(d, off::kPosY);
        out[2] = *detail::F(d, off::kPosZ);
    }

    /**
     * @brief Reads a doodad's scale.
     * @param d  Doodad pointer.
     * @return The scale, or 1.0 when the field is not mapped.
     */
    inline float Scale(void* d)
    {
        return detail::Readable(d, off::kScale + sizeof(float)) ? *detail::F(d, off::kScale) : 1.0f;
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
            const float c0 = *detail::F(d, off::kCenterX);
            const float c1 = *detail::F(d, off::kCenterY);
            const float c2 = *detail::F(d, off::kCenterZ);
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
        mn[0] = *detail::F(d, off::kBBoxMinX); mn[1] = *detail::F(d, off::kBBoxMinY); mn[2] = *detail::F(d, off::kBBoxMinZ);
        mx[0] = *detail::F(d, off::kBBoxMaxX); mx[1] = *detail::F(d, off::kBBoxMaxY); mx[2] = *detail::F(d, off::kBBoxMaxZ);
        return true;
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
        const float* src = detail::F(inst, off::kInstWorldMatrix);
        if (!detail::Readable(src, 16 * sizeof(float))) return false;
        for (int i = 0; i < 16; ++i) m[i] = src[i];
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
        void* inst = Instance(d);
        if (inst && detail::Writable(detail::F(inst, off::kInstWorldMatrix), 16 * sizeof(float)))
            for (int i = 0; i < 16; ++i) detail::F(inst, off::kInstWorldMatrix)[i] = m[i];

        if (detail::Writable(detail::F(d, off::kWorldMatrix), 16 * sizeof(float)))
            for (int i = 0; i < 16; ++i) detail::F(d, off::kWorldMatrix)[i] = m[i];
        if (detail::Writable(detail::F(d, off::kPosX), 3 * sizeof(float)))
        {
            *detail::F(d, off::kPosX) = m[12];
            *detail::F(d, off::kPosY) = m[13];
            *detail::F(d, off::kPosZ) = m[14];
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
        void* inst = Instance(d);
        if (inst && detail::Writable(detail::F(inst, off::kInstTransX), 3 * sizeof(float)))
        {
            *detail::F(inst, off::kInstTransX) = p[0];
            *detail::F(inst, off::kInstTransY) = p[1];
            *detail::F(inst, off::kInstTransZ) = p[2];
        }
        if (detail::Writable(detail::F(d, off::kPosX), 3 * sizeof(float)))
        {
            *detail::F(d, off::kPosX) = p[0];
            *detail::F(d, off::kPosY) = p[1];
            *detail::F(d, off::kPosZ) = p[2];
        }
        if (detail::Writable(detail::F(d, off::kWorldMatrixTransX), 3 * sizeof(float)))
        {
            *detail::F(d, off::kWorldMatrixTransX) = p[0];
            *detail::F(d, off::kWorldMatrixTransY) = p[1];
            *detail::F(d, off::kWorldMatrixTransZ) = p[2];
        }
    }

    /** @brief Adds the doodad bindings to the enumerable catalog. */
    inline void RegisterCatalog()
    {
        Register({ "Doodad::ChunkAt", aoff::kGetChunk, "void*(float pos[3]) - chunk holding doodads" });
    }
}
