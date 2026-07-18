// Generic M3-family model ops: ribbon-to-mesh conversion and animation-library track lookup.
// Neither touches a reader's per-chunk struct versions; both operate on the parsed Model /
// AnimLib and the MD34 container's own reference-table mechanics (stable across the family).
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

#include "M3Model.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace wxl::modern::assets::m3
{
    void RibbonsToMesh(Model& out, float tiltDeg, float lenScale, float baseWidthScale)
    {
        // tip fade: length bands of decreasing batch alpha (additive blending
        // smooths the steps); a geometric taper squashes the texture instead.
        // the falloff curve reaches near zero so the tip has no visible edge
        constexpr int kBands = 64;
        constexpr int kSegPerBand = 1;

        if (lenScale <= 0) lenScale = 1.0f;
        if (baseWidthScale <= 0) baseWidthScale = 1.0f;
        const float rad = tiltDeg * 3.14159265f / 180.0f;
        const Vec3 dirL = { std::sin(rad), 0.0f, std::cos(rad) };
        const Vec3 widL = { std::cos(rad), 0.0f, -std::sin(rad) };

        for (const Ribbon& r : out.ribbons)
        {
            if (r.bone >= out.irefs.size() || r.width <= 0 || r.length <= 0) continue;
            const Mat4 B = MatInverse(out.irefs[r.bone]);
            const uint16_t lookupIdx = uint16_t(out.lookup.size());
            out.lookup.push_back(uint16_t(r.bone));

            for (int band = 0; band < kBands; ++band)
            {
                const uint32_t firstVert = uint32_t(out.verts.size());
                const uint32_t firstFace = uint32_t(out.faces.size());
                for (int s = 0; s <= kSegPerBand; ++s)
                {
                    const float t = (band + float(s) / kSegPerBand) / kBands;
                    const float len = r.length * lenScale;
                    // width interpolates continuously (per-vertex t, not per-band)
                    // from baseWidthScale at the base up to full width at the tip
                    // (tip stays unchanged); the silhouette stays smooth even
                    // though alpha is stepped
                    const float widthScale = baseWidthScale + (1.0f - baseWidthScale) * t;
                    for (int e = 0; e < 2; ++e)
                    {
                        Vertex v{};
                        const float w = (e ? 0.5f : -0.5f) * r.width * widthScale;
                        const Vec3 local = { dirL[0] * t * len + widL[0] * w,
                                             0.0f,
                                             dirL[2] * t * len + widL[2] * w };
                        v.pos = RotatePoint(local, B);
                        v.normal = RotateDir({ 0.0f, 1.0f, 0.0f }, B);
                        v.uv0[0] = t * lenScale; v.uv0[1] = e ? 1.0f : 0.0f;
                        v.uv1[0] = v.uv0[0]; v.uv1[1] = v.uv0[1];
                        v.w[0] = 255;
                        v.bl[0] = 0;
                        out.verts.push_back(v);
                        out.vertRegion.push_back(uint16_t(out.regions.size()));
                        for (int a = 0; a < 3; ++a)
                        {
                            out.bmin[a] = std::min(out.bmin[a], v.pos[a]);
                            out.bmax[a] = std::max(out.bmax[a], v.pos[a]);
                        }
                    }
                }
                for (int s = 0; s < kSegPerBand; ++s)
                {
                    const uint16_t a = uint16_t(s * 2), b = uint16_t(s * 2 + 1);
                    const uint16_t c = uint16_t(s * 2 + 2), d = uint16_t(s * 2 + 3);
                    const uint16_t tri[6] = { a, b, c, b, d, c };
                    for (uint16_t idx : tri) out.faces.push_back(idx);
                }

                Region rg{};
                rg.firstVert = firstVert;
                rg.numVerts = uint32_t(kSegPerBand + 1) * 2;
                rg.firstFace = firstFace;
                rg.numFaces = uint32_t(kSegPerBand) * 6;
                rg.numBones = 1;
                rg.firstLookup = lookupIdx;
                out.regions.push_back(rg);

                Batch b{};
                b.region = uint16_t(out.regions.size() - 1);
                b.matIndex = r.mat;
                const float tm = (band + 0.5f) / kBands;
                const float c = std::cos(tm * 1.5707963f);
                b.alpha = band == 0 ? 1.0f : c * c;
                out.batches.push_back(b);
            }
            if (r.mat < out.materials.size()) out.materials[r.mat].sheetUv = true;
        }
        out.ribbons.clear();
    }

    namespace
    {
        inline uint32_t Rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

        // MD34 reference-table entry: {count, index, flags}, resolved through the container's
        // own index table (idxOfs/idxN at file offset 4/8) -- container mechanics, stable across
        // the whole M3 family regardless of which per-chunk struct versions a file carries.
        struct Ref { uint32_t n, index, flags; };
        inline Ref RdRef(const uint8_t* p) { return { Rd32(p), Rd32(p + 4), Rd32(p + 8) }; }
    }

    bool AnimLib::Track(uint32_t stc, uint32_t animId, uint32_t slot, uint32_t width,
                        LibTrack& out) const
    {
        if (stc >= stcs.size()) return false;
        const LibStc& c = stcs[stc];
        const auto it = c.ids.find(animId);
        if (it == c.ids.end()) return false;
        const uint32_t typ = it->second >> 16;
        const uint32_t idx = it->second & 0xFFFF;
        if (typ != slot || idx >= c.sdCount[typ]) return false;

        const uint8_t* so = data.data() + c.sdOffset[typ] + idx * sdStride;
        const Ref frames = RdRef(so);
        const Ref keys   = RdRef(so + 0x14);
        const uint32_t idxOfs = Rd32(data.data() + 4);
        const uint32_t idxN   = Rd32(data.data() + 8);
        if (frames.index >= idxN || keys.index >= idxN) return false;
        const uint32_t fOfs = Rd32(data.data() + idxOfs + frames.index * 16 + 4);
        const uint32_t kOfs = Rd32(data.data() + idxOfs + keys.index * 16 + 4);
        out.times  = reinterpret_cast<const int32_t*>(data.data() + fOfs);
        out.values = data.data() + kOfs;
        out.count  = frames.n < keys.n ? frames.n : keys.n;
        out.width  = width;
        return out.count > 0;
    }
}
