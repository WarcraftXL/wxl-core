// MD34 reader for the HotS chunk-version family: reference-table container, model parse, and
// animation-library parse. See Md34.hpp for why this is HotS-specific and M3Model.hpp for
// the generic types it populates.
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

#include "Md34.hpp"

#include <cstring>

namespace wxl::modern::assets::m3
{
    namespace
    {
        constexpr uint32_t kMagic = 0x4D443334;  // '43DM' little-endian = MD34
        constexpr uint32_t kBoneStride = 160;    // BONE v1
        constexpr uint32_t kRegnStride = 48;     // REGN v5
        constexpr uint32_t kBatStride  = 14;     // BAT_ v1
        constexpr uint32_t kMatStride  = 340;    // MAT_ v19
        constexpr uint32_t kSeqStride  = 92;     // SEQS v2
        constexpr uint32_t kStgStride  = 24;     // STG_ v0
        constexpr uint32_t kStcStride  = 204;    // STC_ v4
        constexpr uint32_t kParStride  = 1496;   // PAR_ v24
        constexpr uint32_t kRibStride  = 760;    // RIB_ v9
        constexpr uint32_t kSdStride   = 32;     // SDxx sequence-data holder

        inline uint32_t Rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
        inline int32_t  RdS32(const uint8_t* p) { int32_t v; std::memcpy(&v, p, 4); return v; }
        inline uint16_t Rd16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
        inline int16_t  RdS16(const uint8_t* p) { int16_t v; std::memcpy(&v, p, 2); return v; }
        inline float    RdF(const uint8_t* p)  { float v; std::memcpy(&v, p, 4); return v; }

        struct Ref { uint32_t n, index, flags; };
        inline Ref RdRef(const uint8_t* p) { return { Rd32(p), Rd32(p + 4), Rd32(p + 8) }; }

        struct Entry { uint32_t tag, offset, n, version; };

        // The reference-table container: tag/offset/count/version entries the file indexes.
        struct Reader
        {
            const uint8_t*     data = nullptr;
            size_t             size = 0;
            std::vector<Entry> entries;
            uint32_t           modlOffset = 0;

            bool Open(const uint8_t* bytes, size_t byteCount)
            {
                data = bytes;
                size = byteCount;
                if (size < 24 || Rd32(data) != kMagic) return false;
                const uint32_t idxOfs = Rd32(data + 4);
                const uint32_t idxN   = Rd32(data + 8);
                if (size < size_t(idxOfs) + size_t(idxN) * 16) return false;
                entries.resize(idxN);
                for (uint32_t i = 0; i < idxN; ++i)
                {
                    const uint8_t* e = data + idxOfs + i * 16;
                    // tag bytes are stored reversed; keep the canonical forward value
                    entries[i] = { uint32_t(e[0]) | uint32_t(e[1]) << 8 | uint32_t(e[2]) << 16 |
                                   uint32_t(e[3]) << 24, Rd32(e + 4), Rd32(e + 8), Rd32(e + 12) };
                }
                const Ref modl = RdRef(data + 12);
                if (modl.index >= idxN) return false;
                modlOffset = entries[modl.index].offset;
                return true;
            }

            const Entry* At(const Ref& r) const
            {
                return (r.index < entries.size()) ? &entries[r.index] : nullptr;
            }

            uint32_t Offset(const Ref& r) const
            {
                const Entry* e = At(r);
                return e ? e->offset : 0;
            }

            std::string String(const Ref& r) const
            {
                if (!r.n) return {};
                const Entry* e = At(r);
                if (!e || size_t(e->offset) + r.n > size) return {};
                const char* s = reinterpret_cast<const char*>(data + e->offset);
                size_t len = 0;
                while (len < r.n && s[len]) ++len;
                return std::string(s, len);
            }
        };

        // Layer slots probed for a material's image: diffuse first, emissive as the
        // fallback for pure-FX materials that carry no diffuse.
        std::string MaterialImage(const Reader& rd, uint32_t matOfs,
                                  std::initializer_list<uint32_t> slots = { 0u, 4u },
                                  uint32_t* layerOfs = nullptr)
        {
            for (uint32_t slot : slots)
            {
                const Ref lref = RdRef(rd.data + matOfs + 0x34 + slot * 12);
                if (!lref.n) continue;
                const Entry* le = rd.At(lref);
                if (!le) continue;
                std::string img = rd.String(RdRef(rd.data + le->offset + 0x004));
                if (!img.empty())
                {
                    if (layerOfs) *layerOfs = le->offset;
                    return img;
                }
            }
            return {};
        }

        uint32_t UvSetCount(uint32_t vflags)
        {
            uint32_t n = 1;
            for (uint32_t bit : { 0x40000u, 0x80000u, 0x100000u })
                if (vflags & bit) ++n;
            return n;
        }

        // vFlags 0x200 = a 4-byte vertex color between the normal and the uv sets
        uint32_t ColorBytes(uint32_t vflags) { return (vflags & 0x200) ? 4 : 0; }
    }

    bool ParseModel(const uint8_t* data, size_t size, const Mat4& frame, Model& out)
    {
        Reader rd;
        if (!rd.Open(data, size)) return false;
        const uint8_t* mo = data + rd.modlOffset;
        const auto ref = [&](uint32_t off) { return RdRef(mo + off); };

        const uint32_t vflags = Rd32(mo + 0x60);
        const uint32_t vsize  = 32 + 4 * (UvSetCount(vflags) - 1) + ColorBytes(vflags);

        // bounds: transform the box corners into the model frame
        Vec3 bmin = { RdF(mo + 0x88), RdF(mo + 0x8C), RdF(mo + 0x90) };
        Vec3 bmax = { RdF(mo + 0x94), RdF(mo + 0x98), RdF(mo + 0x9C) };
        out.radius = RdF(mo + 0xA0);
        bool first = true;
        for (int cx = 0; cx < 2; ++cx)
            for (int cy = 0; cy < 2; ++cy)
                for (int cz = 0; cz < 2; ++cz)
                {
                    const Vec3 c = RotatePoint({ cx ? bmax[0] : bmin[0], cy ? bmax[1] : bmin[1],
                                                 cz ? bmax[2] : bmin[2] }, frame);
                    if (first) { out.bmin = out.bmax = c; first = false; continue; }
                    for (int a = 0; a < 3; ++a)
                    {
                        if (c[a] < out.bmin[a]) out.bmin[a] = c[a];
                        if (c[a] > out.bmax[a]) out.bmax[a] = c[a];
                    }
                }

        // bones
        const Ref bonesRef = ref(0x050);
        const uint32_t bbase = rd.Offset(bonesRef);
        out.bones.resize(bonesRef.n);
        for (uint32_t i = 0; i < bonesRef.n; ++i)
        {
            const uint8_t* bo = data + bbase + i * kBoneStride;
            BoneRec& b = out.bones[i];
            b.name    = rd.String(RdRef(bo + 0x04));
            b.parent  = RdS16(bo + 0x14);
            b.locId   = Rd32(bo + 0x1C);
            b.initLoc = { RdF(bo + 0x20), RdF(bo + 0x24), RdF(bo + 0x28) };
            b.rotId   = Rd32(bo + 0x40);
            b.initRot = { RdF(bo + 0x44), RdF(bo + 0x48), RdF(bo + 0x4C), RdF(bo + 0x50) };
            b.scaleId = Rd32(bo + 0x6C);
            b.initScale = { RdF(bo + 0x70), RdF(bo + 0x74), RdF(bo + 0x78) };
        }

        // inverse rest matrices, conjugated into the model frame
        const Ref irefRef = ref(0x288);
        const uint32_t ibase = rd.Offset(irefRef);
        const Mat4 frameInv = MatInverse(frame);
        out.irefs.resize(irefRef.n);
        for (uint32_t i = 0; i < irefRef.n; ++i)
        {
            Mat4 m;
            std::memcpy(&m.m[0][0], data + ibase + i * 64, 64);
            out.irefs[i] = MatMul(frameInv, m);
        }

        // bone lookup
        const Ref lookupRef = ref(0x07C);
        const uint32_t lbase = rd.Offset(lookupRef);
        out.lookup.resize(lookupRef.n);
        for (uint32_t i = 0; i < lookupRef.n; ++i)
            out.lookup[i] = Rd16(data + lbase + i * 2);

        // vertices (uv stays raw here; the per-region pass below applies mult/offset)
        const Ref vref = ref(0x064);
        const uint32_t nverts = vsize ? vref.n / vsize : 0;
        const uint32_t vbase  = rd.Offset(vref);
        const uint32_t uvSets = UvSetCount(vflags);
        out.verts.resize(nverts);
        for (uint32_t i = 0; i < nverts; ++i)
        {
            const uint8_t* vo = data + vbase + i * vsize;
            Vertex& v = out.verts[i];
            v.pos = RotatePoint({ RdF(vo), RdF(vo + 4), RdF(vo + 8) }, frame);
            std::memcpy(v.w, vo + 0x0C, 4);
            std::memcpy(v.bl, vo + 0x10, 4);
            v.normal = RotateDir({ vo[0x14] / 255.0f * 2 - 1, vo[0x15] / 255.0f * 2 - 1,
                                   vo[0x16] / 255.0f * 2 - 1 }, frame);
            const uint32_t uvBase = 0x18 + ColorBytes(vflags);
            v.uv0[0] = float(RdS16(vo + uvBase));
            v.uv0[1] = float(RdS16(vo + uvBase + 2));
            if (uvSets >= 2)
            {
                v.uv1[0] = float(RdS16(vo + uvBase + 4));
                v.uv1[1] = float(RdS16(vo + uvBase + 6));
            }
            else v.uv1[0] = v.uv1[1] = 0.0f;
        }

        // division: faces, regions (with the per-region uv decode), batches
        const Ref divRef = ref(0x070);
        const uint8_t* dv = data + rd.Offset(divRef);
        const Ref facesRef = RdRef(dv + 0x00);
        const Ref regnRef  = RdRef(dv + 0x0C);
        const Ref batRef   = RdRef(dv + 0x18);

        const uint32_t fbase = rd.Offset(facesRef);
        out.faces.resize(facesRef.n);
        for (uint32_t i = 0; i < facesRef.n; ++i)
            out.faces[i] = Rd16(data + fbase + i * 2);

        const uint32_t rbase = rd.Offset(regnRef);
        out.regions.resize(regnRef.n);
        out.vertRegion.assign(nverts, 0);
        for (uint32_t i = 0; i < regnRef.n; ++i)
        {
            const uint8_t* ro = data + rbase + i * kRegnStride;
            Region& r = out.regions[i];
            r.firstVert   = Rd32(ro + 0x08);
            r.numVerts    = Rd32(ro + 0x0C);
            r.firstFace   = Rd32(ro + 0x10);
            r.numFaces    = Rd32(ro + 0x14);
            r.numBones    = Rd16(ro + 0x18);
            r.firstLookup = Rd16(ro + 0x1A);

            // uv = raw / 32768 * uvwMult + uvwOffset, per region
            const float scale = RdF(ro + 0x28) / 32768.0f;
            const float uvoff = RdF(ro + 0x2C);
            for (uint32_t vi = r.firstVert; vi < r.firstVert + r.numVerts && vi < nverts; ++vi)
            {
                Vertex& v = out.verts[vi];
                v.uv0[0] = v.uv0[0] * scale + uvoff;
                v.uv0[1] = v.uv0[1] * scale + uvoff;
                v.uv1[0] = v.uv1[0] * scale + uvoff;
                v.uv1[1] = v.uv1[1] * scale + uvoff;
                out.vertRegion[vi] = uint16_t(i);
            }
        }

        // materials (standard set only) + the material-reference indirection for batches
        const Ref matmRef = ref(0x12C);
        const uint32_t matmBase = rd.Offset(matmRef);
        const Ref matsRef = ref(0x138);
        const uint32_t mbase = rd.Offset(matsRef);
        out.materials.resize(matsRef.n);
        for (uint32_t i = 0; i < matsRef.n; ++i)
        {
            const uint32_t ao = mbase + i * kMatStride;
            out.materials[i].blend   = Rd32(data + ao + 0x14);
            uint32_t layerOfs = 0;
            out.materials[i].diffuse = MaterialImage(rd, ao, { 0u, 4u }, &layerOfs);
            // uv-offset animref of the picked layer drives the scroll resolution
            if (layerOfs) out.materials[i].uvAnim = Rd32(data + layerOfs + 0xC0);
        }

        const uint32_t btbase = rd.Offset(batRef);
        out.batches.resize(batRef.n);
        for (uint32_t i = 0; i < batRef.n; ++i)
        {
            const uint8_t* bo = data + btbase + i * kBatStride;
            Batch& b = out.batches[i];
            b.region = Rd16(bo + 0x04);
            const uint32_t matref = Rd16(bo + 0x0A);
            const uint32_t mtype  = Rd32(data + matmBase + matref * 8);
            const uint32_t midx   = Rd32(data + matmBase + matref * 8 + 4);
            b.matIndex = (mtype == 1 && midx < out.materials.size()) ? midx : 0;
        }

        // particle emitters (static initValues; PAR_ v24 only)
        const Ref parRef = ref(0x1BC);
        if (parRef.n)
        {
            const Entry* pe = rd.At(parRef);
            if (pe && pe->version == 24)
            {
                const auto initF = [&](uint32_t o) { return RdF(data + o + 0x08); };
                out.particles.resize(parRef.n);
                for (uint32_t i = 0; i < parRef.n; ++i)
                {
                    const uint32_t po = pe->offset + i * kParStride;
                    Emitter& em = out.particles[i];
                    em.bone = Rd32(data + po);
                    const uint32_t matref = Rd32(data + po + 0x04);
                    const uint32_t mtype  = Rd32(data + matmBase + matref * 8);
                    const uint32_t midx   = Rd32(data + matmBase + matref * 8 + 4);
                    em.mat = (mtype == 1 && midx < out.materials.size()) ? midx : 0;
                    // particle atlas: FX materials leave the diffuse slot empty and put
                    // the flipbook in the mask slot; the emissive slot is a body map
                    em.image = MaterialImage(rd, mbase + em.mat * kMatStride,
                                             { 0u, 8u, 4u });
                    em.speed[0] = initF(po + 0x0C);
                    em.speed[1] = initF(po + 0x20);
                    em.angle[0] = initF(po + 0x34);
                    em.angle[1] = initF(po + 0x48);
                    em.spread[0] = initF(po + 0x5C);
                    em.spread[1] = initF(po + 0x70);
                    em.life[0]  = initF(po + 0x84);
                    em.life[1]  = initF(po + 0x98);
                    em.zacc     = RdF(data + po + 0xB8);
                    em.mid[0] = RdF(data + po + 0xBC);
                    em.mid[1] = RdF(data + po + 0xC0);
                    em.mid[2] = RdF(data + po + 0xC4);
                    em.sizes[0] = RdF(data + po + 0xDC + 0x08);
                    em.sizes[1] = RdF(data + po + 0xDC + 0x0C);
                    em.sizes[2] = RdF(data + po + 0xDC + 0x10);
                    for (int c = 0; c < 3; ++c)
                        std::memcpy(em.colors[c], data + po + 0x124 + c * 0x14 + 0x08, 4);
                    em.rate = initF(po + 0x194);
                    em.rateAnimId = Rd32(data + po + 0x194 + 0x04);
                    em.areaSize[0] = RdF(data + po + 0x1AC + 0x08);
                    em.areaSize[1] = RdF(data + po + 0x1AC + 0x0C);
                    em.areaSize[2] = RdF(data + po + 0x1AC + 0x10);
                    em.areaRadius  = initF(po + 0x1F4);
                    em.cellFirst = data[po + 0x2D0];
                    em.cellLast  = data[po + 0x2D1];
                    em.cols = Rd16(data + po + 0x2D8);
                    em.rows = Rd16(data + po + 0x2DA);
                }
            }
        }

        // ribbon emitters (static initValues; RIB_ v9 only)
        const Ref ribRef = ref(0x1D4);
        if (ribRef.n)
        {
            const Entry* re = rd.At(ribRef);
            if (re && re->version == 9)
            {
                out.ribbons.resize(ribRef.n);
                for (uint32_t i = 0; i < ribRef.n; ++i)
                {
                    const uint32_t ro = re->offset + i * kRibStride;
                    Ribbon& rb = out.ribbons[i];
                    rb.bone = Rd32(data + ro);
                    const uint32_t matref = Rd32(data + ro + 0x04);
                    const uint32_t mtype  = Rd32(data + matmBase + matref * 8);
                    const uint32_t midx   = Rd32(data + matmBase + matref * 8 + 4);
                    rb.mat = (mtype == 1 && midx < out.materials.size()) ? midx : 0;
                    rb.image = MaterialImage(rd, mbase + rb.mat * kMatStride,
                                             { 0u, 8u, 4u });
                    rb.width  = RdF(data + ro + 0xDC + 0x08);
                    rb.length = RdF(data + ro + 0x1A4 + 0x08);
                }
            }
        }
        return true;
    }

    bool ParseAnimLib(std::vector<uint8_t> bytes, AnimLib& out)
    {
        out.data = std::move(bytes);
        out.sdStride = kSdStride; // HotS SD holder stride; AnimLib::Track (M3Model.cpp) reads it back
        Reader rd;
        if (!rd.Open(out.data.data(), out.data.size())) return false;
        const uint8_t* data = out.data.data();
        const uint8_t* mo = data + rd.modlOffset;
        const auto ref = [&](uint32_t off) { return RdRef(mo + off); };

        const Ref bonesRef = ref(0x050);
        const uint32_t bbase = rd.Offset(bonesRef);
        for (uint32_t i = 0; i < bonesRef.n; ++i)
        {
            const uint8_t* bo = data + bbase + i * kBoneStride;
            LibBone b;
            b.loc   = Rd32(bo + 0x1C);
            b.initLoc = { RdF(bo + 0x20), RdF(bo + 0x24), RdF(bo + 0x28) };
            b.rot   = Rd32(bo + 0x40);
            b.initRot = { RdF(bo + 0x44), RdF(bo + 0x48), RdF(bo + 0x4C), RdF(bo + 0x50) };
            b.scale = Rd32(bo + 0x6C);
            b.initScale = { RdF(bo + 0x70), RdF(bo + 0x74), RdF(bo + 0x78) };
            out.bones.emplace(rd.String(RdRef(bo + 0x04)), b);
        }

        const Ref seqsRef = ref(0x10);
        const Ref stcsRef = ref(0x1C);
        const Ref stgsRef = ref(0x28);
        const uint32_t sbase = rd.Offset(seqsRef);
        const uint32_t cbase = rd.Offset(stcsRef);
        const uint32_t gbase = rd.Offset(stgsRef);

        out.seqs.resize(seqsRef.n);
        for (uint32_t i = 0; i < seqsRef.n; ++i)
        {
            const uint8_t* so = data + sbase + i * kSeqStride;
            const uint8_t* go = data + gbase + i * kStgStride;
            LibSeq& s = out.seqs[i];
            s.name  = rd.String(RdRef(so + 0x08));
            s.start = Rd32(so + 0x14);
            s.end   = Rd32(so + 0x18);
            s.flags = Rd32(so + 0x20);
            const Ref sref = RdRef(go + 0x0C);
            const uint32_t sOfs = rd.Offset(sref);
            s.stcs.resize(sref.n);
            for (uint32_t k = 0; k < sref.n; ++k)
                s.stcs[k] = Rd32(data + sOfs + k * 4);
        }

        out.stcs.resize(stcsRef.n);
        for (uint32_t i = 0; i < stcsRef.n; ++i)
        {
            const uint8_t* co = data + cbase + i * kStcStride;
            LibStc& c = out.stcs[i];
            c.conc = Rd16(co + 0x0C);
            const Ref idsRef  = RdRef(co + 0x14);
            const Ref refsRef = RdRef(co + 0x20);
            const uint32_t iOfs = rd.Offset(idsRef);
            const uint32_t rOfs = rd.Offset(refsRef);
            const uint32_t n = idsRef.n < refsRef.n ? idsRef.n : refsRef.n;
            for (uint32_t k = 0; k < n; ++k)
                c.ids.emplace(Rd32(data + iOfs + k * 4), Rd32(data + rOfs + k * 4));
            for (uint32_t k = 0; k < 13; ++k)
            {
                const Ref sd = RdRef(co + 0x30 + k * 12);
                const Entry* e = rd.At(sd);
                c.sdOffset[k] = e ? e->offset : 0;
                c.sdCount[k]  = sd.n;
            }
        }
        return true;
    }
}
