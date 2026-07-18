// Masks the source blend time and remaps out-of-range sequence ids onto client ids.
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

#include "Animations.hpp"

#include <cstdint>

namespace wxl::modern::assets::m2::animations
{
    namespace fmt = wxl::structure::m2;

    namespace
    {
        // Max animation id the client engine resolves; a source sequence id above this needs a remap.
        constexpr uint16_t kClientMaxAnimId = 505;

        /**
         * @brief Finds the index of the first sequence whose id equals anim.
         * @param seqs   Sequence array.
         * @param count  Sequence count.
         * @param anim   Animation id to match.
         * @return Index of the first match, or -1 if none.
         */
        int16_t AnimationIndex(const fmt::M2Sequence* seqs, uint32_t count, uint16_t anim)
        {
            for (uint32_t i = 0; i < count; ++i)
                if (seqs[i].id == anim) return static_cast<int16_t>(i);
            return -1;
        }

        /**
         * @brief Points lookup[newId] at newPos if it still holds oldPos, else rewrites the first
         *        entry holding oldPos.
         * @param lookup       Sequence lookup array.
         * @param lookupCount  Lookup entry count.
         * @param oldPos       Position the entry currently holds.
         * @param newId        Lookup index to prefer.
         * @param newPos       Position to write.
         */
        void ReplaceAnimLookup(int16_t* lookup, uint32_t lookupCount, int16_t oldPos, uint16_t newId,
                               int16_t newPos)
        {
            if (newId < lookupCount && lookup[newId] == oldPos) { lookup[newId] = newPos; return; }
            for (uint32_t i = 0; i < lookupCount; ++i)
                if (lookup[i] == oldPos) { lookup[i] = newPos; break; }
        }
    }

    /**
     * @brief Fixes every sequence in place.
     *
     * Two transforms over the sequence array: masks blendTime to its low u16 (the source splits it
     * into in|out, read whole it is a huge blend so transitions never complete), and remaps a curated
     * set of ids above the client max to a client id, patching the lookup so the engine still resolves them.
     * @param md  Model header (pre-parse, offsets model-relative).
     */
    void Fix(fmt::M2Header* md)
    {
        if (!md->sequences.count || !md->sequences.offset) return;
        auto* seqs = reinterpret_cast<fmt::M2Sequence*>(md->base() + md->sequences.offset);
        uint32_t count = md->sequences.count;

        int16_t* lookup = nullptr;
        uint32_t lookupCount = 0;
        if (md->sequenceLookup.count && md->sequenceLookup.offset)
        {
            lookup      = reinterpret_cast<int16_t*>(md->base() + md->sequenceLookup.offset);
            lookupCount = md->sequenceLookup.count;
        }

        for (uint32_t i = 0; i < count; ++i)
        {
            uint16_t id = seqs[i].id;
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
                if (id != anim && lookup)
                {
                    ReplaceAnimLookup(lookup, lookupCount, AnimationIndex(seqs, count, anim), anim,
                                      static_cast<int16_t>(i));
                    seqs[i].id = anim;
                }
            }

            seqs[i].blendTime &= 0xFFFF;
        }
    }
}
