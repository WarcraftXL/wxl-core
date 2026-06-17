// Copyright (C) 2026 WraithEngine
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

#include "AdtMerge.hpp"

#include "AdtTexScale.hpp"
#include "Logger.hpp"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

// Modern terrain tiles are split into root + _obj0 + _tex0 (+ _obj1/_lod) files; the target client
// (335) reads ONE monolithic .adt (MVER|MHDR|MCIN[256]|map chunks|MCNK[256]). The merge runs here on
// the bytes the host already read: root + the two siblings come in as spans, the assembled tile goes
// out. Modern per-MCNK data is kept as far as the target can consume it; the few features it still
// caps (texture layers > 4) are clamped (the >4 case is the multi-pass terrain work, runtime-side).
// The transform is DATA-GATED (chunk presence / field values), so one path serves every modern source.
namespace wraith::structure::adt
{
    namespace
    {
        constexpr uint32_t CC(char a, char b, char c, char d)
        {
            return (uint32_t(uint8_t(a)) << 24) | (uint32_t(uint8_t(b)) << 16) |
                   (uint32_t(uint8_t(c)) << 8) | uint32_t(uint8_t(d));
        }
        constexpr uint32_t MVER=CC('M','V','E','R'), MHDR=CC('M','H','D','R'), MCIN=CC('M','C','I','N');
        constexpr uint32_t MTEX=CC('M','T','E','X'), MTXF=CC('M','T','X','F'), MTXP=CC('M','T','X','P');
        constexpr uint32_t MDID=CC('M','D','I','D'), MHID=CC('M','H','I','D');
        constexpr uint32_t MFBO=CC('M','F','B','O'), MH2O=CC('M','H','2','O');
        constexpr uint32_t MMDX=CC('M','M','D','X'), MMID=CC('M','M','I','D');
        constexpr uint32_t MWMO=CC('M','W','M','O'), MWID=CC('M','W','I','D');
        constexpr uint32_t MDDF=CC('M','D','D','F'), MODF=CC('M','O','D','F');
        constexpr uint32_t MCNK=CC('M','C','N','K'), MCVT=CC('M','C','V','T'), MCCV=CC('M','C','C','V');
        constexpr uint32_t MCNR=CC('M','C','N','R'), MCLY=CC('M','C','L','Y'), MCRF=CC('M','C','R','F');
        constexpr uint32_t MCSH=CC('M','C','S','H'), MCAL=CC('M','C','A','L'), MCSE=CC('M','C','S','E');
        constexpr uint32_t MCRD=CC('M','C','R','D'), MCRW=CC('M','C','R','W');

        uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
        uint32_t rd32(const uint8_t* p) { return p[0] | (p[1]<<8) | (p[2]<<16) | (uint32_t(p[3])<<24); }
        uint64_t rd64(const uint8_t* p) { return rd32(p) | (uint64_t(rd32(p+4)) << 32); }

        // Sequential output builder; offsets back-patched once the layout is known.
        struct Out
        {
            std::vector<uint8_t> d;
            uint32_t tell() const { return uint32_t(d.size()); }
            void u32(uint32_t v) { for (int i=0;i<4;++i) d.push_back(uint8_t(v>>(i*8))); }
            void raw(const uint8_t* p, uint32_t n) { if (n) d.insert(d.end(), p, p+n); }
            void patch16(uint32_t at, uint16_t v) { d[at]=uint8_t(v); d[at+1]=uint8_t(v>>8); }
            void patch32(uint32_t at, uint32_t v) { for (int i=0;i<4;++i) d[at+i]=uint8_t(v>>(i*8)); }
            uint32_t chunk(uint32_t magic, const uint8_t* data, uint32_t n)
            { uint32_t s=tell(); u32(magic); u32(n); if (data) raw(data,n); else d.insert(d.end(), n, 0); return s; }
        };

        bool find(const uint8_t* buf, uint32_t len, uint32_t magic, uint32_t& dataOff, uint32_t& size, uint32_t start=0)
        {
            uint32_t o = start;
            while (o + 8 <= len)
            {
                uint32_t m = rd32(buf+o), sz = rd32(buf+o+4);
                if (o + 8 + sz > len) break;
                if (m == magic) { dataOff = o+8; size = sz; return true; }
                o += 8 + sz;
            }
            return false;
        }

        void collectMcnk(const uint8_t* buf, uint32_t len, uint32_t out[256][2], int& count)
        {
            count = 0;
            uint32_t o = 0;
            while (o + 8 <= len && count < 256)
            {
                uint32_t m = rd32(buf+o), sz = rd32(buf+o+4);
                if (o + 8 + sz > len) break;
                if (m == MCNK) { out[count][0]=o+8; out[count][1]=sz; ++count; }
                o += 8 + sz;
            }
        }

        // Find a sub-chunk inside an MCNK data region [base, base+len).
        bool sub(const uint8_t* buf, uint32_t base, uint32_t len, uint32_t magic, uint32_t& dataOff, uint32_t& size)
        {
            uint32_t o = base, end = base + len;
            while (o + 8 <= end)
            {
                uint32_t m = rd32(buf+o), sz = rd32(buf+o+4);
                if (o + 8 + sz > end) break;
                if (m == magic) { dataOff = o+8; size = sz; return true; }
                o += 8 + sz;
            }
            return false;
        }

        // 64-bit hi-res hole mask (8x8) -> 16-bit lo-res (4x4), the fallback the target reads.
        uint16_t holesHiToLo(uint64_t hi)
        {
            if (!hi) return 0;
            uint16_t lo = 0;
            for (int i = 0; i < 64; ++i)
                if ((hi >> i) & 1) { int x=(i%8)/2, y=i/16; lo |= uint16_t(1 << (x + y*4)); }
            return lo;
        }

        // Decode one terrain alpha map to a canonical 64x64 4-bit (2048-byte) map appended to `out`. The
        // genformat (4-bit vs 8-bit MCAL) is MAP-WIDE (the WDT MPHD big_alpha bit), and a map mixes native
        // tiles (served raw, 4-bit) with these merged tiles - so every merged tile MUST also be 4-bit, or the
        // native tiles' 2048-byte MCAL gets read as 8-bit (4096) and the bake walks off the buffer (crash /
        // mid-chunk seam). Retail source maps are 8-bit (often 0x200-RLE); they are decoded to 8-bit then
        // packed down to 4-bit (high nibble). The source read is bounded; the output is always exactly 2048.
        // `compressed` = MCLY flag 0x200 (RLE: ctrl byte, high bit = fill run, else copy run).
        void decodeAlpha2048(const uint8_t* src, uint32_t srcLen, bool compressed, std::vector<uint8_t>& out)
        {
            uint8_t tmp[4096];                                                    // 8-bit staging
            std::memset(tmp, 0, sizeof tmp);
            if (compressed)
            {
                uint32_t si = 0, di = 0;
                while (di < 4096 && si < srcLen)
                {
                    const uint8_t ctl = src[si++];
                    uint32_t n = ctl & 0x7F;
                    if (ctl & 0x80) { if (si >= srcLen) break; const uint8_t v = src[si++]; while (n-- && di < 4096) tmp[di++] = v; }
                    else            { while (n-- && di < 4096 && si < srcLen) tmp[di++] = src[si++]; }
                }
            }
            else if (srcLen >= 4096) std::memcpy(tmp, src, 4096);                 // already 8-bit
            else if (srcLen >= 2048)                                              // already 4-bit -> expand for uniform packing
                for (uint32_t i = 0; i < 2048; ++i) { const uint8_t b = src[i]; tmp[i*2] = uint8_t((b & 0xF) * 17); tmp[i*2+1] = uint8_t((b >> 4) * 17); }
            else if (srcLen) std::memcpy(tmp, src, srcLen);                       // short: keep what exists

            const size_t base = out.size();
            out.resize(base + 2048);
            uint8_t* dst = out.data() + base;
            for (uint32_t i = 0; i < 2048; ++i)                                   // pack two 8-bit texels -> one byte (a low nibble, b high)
                dst[i] = uint8_t((tmp[i*2] >> 4) | ((tmp[i*2+1] >> 4) << 4));
        }

        // Assemble one monolithic MCNK from the three split pieces; fill mcin[id] with its file offset + size.
        int buildMcnk(Out& o, int id,  const uint8_t* rb, uint32_t rOff, uint32_t rLen,
                                        const uint8_t* tb, uint32_t tOff, uint32_t tLen,
                                        const uint8_t* ob, uint32_t oOff, uint32_t oLen,
                                        uint32_t mcin[256][2])
        {
            const uint32_t start = o.tell();
            o.u32(MCNK);
            const uint32_t sizePos = o.tell(); o.u32(0);
            const uint32_t hdrPos = o.tell();
            o.raw(rb + rOff, 0x80);                  // root 0x80 header, fixed up below

            const uint8_t* rh = rb + rOff;
            const uint32_t flags = rd32(rh + 0x00);
            const uint32_t geom = rOff + 0x80;
            const uint32_t geomLen = rLen - 0x80;
            uint32_t d, s;

            uint32_t ofsMCVT = o.tell() - start;
            if (sub(rb, geom, geomLen, MCVT, d, s)) o.chunk(MCVT, rb+d, s); else o.chunk(MCVT, nullptr, 0);

            uint32_t ofsMCCV = 0;
            if (sub(rb, geom, geomLen, MCCV, d, s)) { ofsMCCV = o.tell()-start; o.chunk(MCCV, rb+d, s); }

            uint32_t ofsMCNR = o.tell() - start;
            {
                uint8_t nrm[448]; memset(nrm, 0, sizeof nrm);
                if (sub(rb, geom, geomLen, MCNR, d, s)) memcpy(nrm, rb+d, s < 448 ? s : 448);
                o.u32(MCNR); o.u32(435); o.raw(nrm, 448);
            }

            // MCLY (tex0). Clamp to 4 layers: the native terrain build assumes <= 4 in several places
            // (the render-node slot array AND the alpha-bake stack array), so serving more crashes the
            // native build. The >4-layer tail lands once the DLL multi-pass (bake + draw) handles it; the
            // un-clamp must ship together with that, never alone.
            uint32_t ofsMCLY = 0; int nLayer = 0, rawLayers = 0;
            std::vector<uint8_t> newMcal; // re-packed alpha maps (2048 bytes per use-alpha layer, contiguous)
            uint32_t mcalOff = 0, mcalSz = 0; const uint8_t* mcalBase = nullptr;
            if (tb && sub(tb, tOff, tLen, MCAL, mcalOff, mcalSz)) mcalBase = tb + mcalOff;
            if (tb && sub(tb, tOff, tLen, MCLY, d, s))
            {
                int layers = int(s / 0x10), keep = layers;
                rawLayers = layers; // pre-clamp count (probe: which chunks exceed the native 4-layer cap)
                if (keep > 4) keep = 4;
                ofsMCLY = o.tell() - start;
                o.u32(MCLY); o.u32(uint32_t(keep * 0x10));
                for (int i = 0; i < keep; ++i)
                {
                    const uint8_t* L = tb + d + i*0x10;
                    const uint32_t flags = rd32(L+0x04);
                    const uint32_t ground = rd32(L+0x0C);
                    uint32_t newOfs = 0;
                    if ((flags & 0x100) && mcalBase) // use_alpha_map: re-pack this layer's map to plain 4096
                    {
                        const uint32_t srcOfs  = rd32(L+0x08);
                        const uint32_t nextOfs = (i+1 < layers) ? rd32(tb + d + (i+1)*0x10 + 0x08) : mcalSz;
                        const uint32_t srcLen  = (srcOfs <= mcalSz)
                            ? ((nextOfs > srcOfs && nextOfs <= mcalSz) ? nextOfs - srcOfs : mcalSz - srcOfs) : 0;
                        newOfs = uint32_t(newMcal.size());
                        decodeAlpha2048(mcalBase + srcOfs, srcLen, (flags & 0x200) != 0, newMcal);
                    }
                    o.u32(rd32(L+0x00));                     // textureId
                    o.u32((flags & 0x7FF) & ~0x200u);           // flags: keep target bits + use_alpha, drop compressed
                    o.u32(newOfs);                              // ofsAlpha into the re-packed MCAL
                    o.u32(ground);                              // GroundEffectTexture id
                }
                nLayer = keep;
            }

            // MCRF = MCRD (doodad refs) ++ MCRW (wmo refs).
            uint32_t ofsMCRF = o.tell() - start, rdOff=0, rdSz=0, rwOff=0, rwSz=0;
            bool hasRD = ob && sub(ob, oOff, oLen, MCRD, rdOff, rdSz);
            bool hasRW = ob && sub(ob, oOff, oLen, MCRW, rwOff, rwSz);
            uint32_t nDoodads = hasRD ? rdSz/4 : 0, nMapObjRefs = hasRW ? rwSz/4 : 0;
            o.u32(MCRF); o.u32((hasRD?rdSz:0) + (hasRW?rwSz:0));
            if (hasRD) o.raw(ob+rdOff, rdSz);
            if (hasRW) o.raw(ob+rwOff, rwSz);

            uint32_t ofsMCSH = 0, sizeMCSH = 0;
            if (tb && sub(tb, tOff, tLen, MCSH, d, s)) { ofsMCSH = o.tell()-start; o.chunk(MCSH, tb+d, s); sizeMCSH = s+8; }

            uint32_t ofsMCAL = o.tell() - start, sizeMCAL = 0;
            if (!newMcal.empty())
            {
                o.chunk(MCAL, newMcal.data(), uint32_t(newMcal.size()));
                sizeMCAL = uint32_t(newMcal.size()) + 8;
            }
            else { o.chunk(MCAL, nullptr, 0); sizeMCAL = 8; }

            uint32_t ofsMCSE = 0, nSnd = 0;
            if (sub(rb, geom, geomLen, MCSE, d, s)) { ofsMCSE = o.tell()-start; o.chunk(MCSE, rb+d, s); nSnd = s/0x1C; }

            uint64_t hiHoles = (flags & 0x10000) ? rd64(rh + 0x14) : 0;
            uint16_t loHoles = hiHoles ? holesHiToLo(hiHoles) : rd16(rh + 0x3C);

            // Clear do_not_fix_alpha_map (0x8000): the tile is served 4-bit (genformat 3, MPHD big_alpha
            // clear, matching the native tiles the map mixes in), the stock Client small-alpha path. The
            // unpack leaves read a full 64x64 4-bit map regardless of this flag, so the clean stock combo is
            // big_alpha-clear; keep only the low 16 target bits.
            o.patch32(hdrPos + 0x00, (flags & 0xFFFF) & ~0x8000u);
            o.patch32(hdrPos + 0x04, uint32_t(id % 16));
            o.patch32(hdrPos + 0x08, uint32_t(id / 16));
            o.patch32(hdrPos + 0x0C, uint32_t(nLayer));
            o.patch32(hdrPos + 0x10, nDoodads);
            o.patch32(hdrPos + 0x14, ofsMCVT);
            o.patch32(hdrPos + 0x18, ofsMCNR);
            o.patch32(hdrPos + 0x1C, ofsMCLY);
            o.patch32(hdrPos + 0x20, ofsMCRF);
            o.patch32(hdrPos + 0x24, ofsMCAL);
            o.patch32(hdrPos + 0x28, sizeMCAL);
            o.patch32(hdrPos + 0x2C, ofsMCSH);
            o.patch32(hdrPos + 0x30, sizeMCSH);
            // +0x34 areaId: kept from the copied root header
            o.patch32(hdrPos + 0x38, nMapObjRefs);
            o.patch16(hdrPos + 0x3C, loHoles);
            o.patch32(hdrPos + 0x58, ofsMCSE);
            o.patch32(hdrPos + 0x5C, nSnd);
            o.patch32(hdrPos + 0x60, 0);
            o.patch32(hdrPos + 0x64, 0);
            o.patch32(hdrPos + 0x74, ofsMCCV);
            // +0x78/+0x7C: hi-res 8x8 hole mask, for the hi-res hole engine patch to read
            o.patch32(hdrPos + 0x78, uint32_t(hiHoles));
            o.patch32(hdrPos + 0x7C, uint32_t(hiHoles >> 32));

            const uint32_t total = o.tell() - start;
            o.patch32(sizePos, total - 8);
            mcin[id][0] = start;
            mcin[id][1] = total;
            return rawLayers;
        }

        // Modern obj0 references placed models by FileDataID in the placement entry's nameId, with no
        // MMDX/MMID (doodads) or MWMO/MWID (map objects) name table. The target reads nameId as an INDEX
        // into that table, so an FDID indexes a wild pointer -> crash. This rebuilds the table: resolve each
        // unique FDID to a path, emit the name blob + offset table, rewrite every entry's nameId to its
        // table index, and clear the FDID flag. `stride`/`flagOff`/`fdidBit` differ per placement type.
        struct PlacementRebuild
        {
            std::vector<uint8_t> names;   // MMDX / MWMO blob
            std::vector<uint8_t> offsets; // MMID / MWID (u32 offsets into `names`)
            std::vector<uint8_t> entries; // rewritten MDDF / MODF
            uint32_t resolved = 0, missing = 0;
        };

        PlacementRebuild rebuildFdidPlacements(const uint8_t* p, uint32_t size, uint32_t stride,
                                               uint32_t flagOff, uint16_t flagAndMask, const char* fallback,
                                               const ResolveCtx& rc)
        {
            PlacementRebuild r;
            r.entries.assign(p, p + size);
            std::vector<uint32_t> seen; // unique FDIDs, in first-seen order
            for (uint32_t e = 0; e + stride <= size; e += stride)
            {
                const uint32_t fdid = rd32(p + e);
                uint32_t idx = 0xFFFFFFFFu;
                for (uint32_t i = 0; i < seen.size(); ++i) if (seen[i] == fdid) { idx = i; break; }
                if (idx == 0xFFFFFFFFu)
                {
                    idx = uint32_t(seen.size());
                    seen.push_back(fdid);
                    std::string path;
                    const bool ok = fdid && rc.resolve && rc.resolve(rc.user, fdid, path) && !path.empty();
                    const char* str = ok ? path.c_str() : fallback;
                    (ok ? r.resolved : r.missing) += 1;
                    const uint32_t off = uint32_t(r.names.size());
                    for (int k = 0; k < 4; ++k) r.offsets.push_back(uint8_t(off >> (k * 8)));
                    r.names.insert(r.names.end(), str, str + std::strlen(str));
                    r.names.push_back(0);
                }
                for (int k = 0; k < 4; ++k) r.entries[e + k] = uint8_t(idx >> (k * 8)); // nameId = table index
                uint16_t fl = uint16_t(r.entries[e + flagOff] | (r.entries[e + flagOff + 1] << 8));
                fl &= flagAndMask; // keep only flags the target understands; drops the FDID bit
                r.entries[e + flagOff]     = uint8_t(fl);
                r.entries[e + flagOff + 1] = uint8_t(fl >> 8);
            }
            return r;
        }
    }

    bool MergeSplitAdt(std::span<const uint8_t> root, std::span<const uint8_t> tex0,
                       std::span<const uint8_t> obj0, std::vector<uint8_t>& out, std::string_view name,
                       const ResolveCtx& rc)
    {
        const bool hasTex = !tex0.empty();
        const bool hasObj = !obj0.empty();
        if (!hasTex && !hasObj) return false; // not a split tile; serve root unchanged

        const uint8_t* rb = root.data(); uint32_t rl = uint32_t(root.size());
        const uint8_t* tb = hasTex ? tex0.data() : nullptr; uint32_t tl = uint32_t(tex0.size());
        const uint8_t* ob = hasObj ? obj0.data() : nullptr; uint32_t ol = uint32_t(obj0.size());

        uint32_t rM[256][2], tM[256][2], oM[256][2]; int rCnt=0, tc=0, oc=0;
        collectMcnk(rb, rl, rM, rCnt);
        if (hasTex) collectMcnk(tb, tl, tM, tc);
        if (hasObj) collectMcnk(ob, ol, oM, oc);
        wraith::core::log::Printf("adt: merge %.*s hasTex=%d hasObj=%d rootMcnk=%d tex=%d obj=%d",
            int(name.size()), name.data(), int(hasTex), int(hasObj), rCnt, tc, oc);
        if (rCnt != 256) return false;

        Out o;
        { uint8_t v[4]={18,0,0,0}; o.chunk(MVER, v, 4); }
        uint32_t mhdr = o.chunk(MHDR, nullptr, 0x40);
        uint32_t mhdrData = mhdr + 8;
        uint32_t mcin = o.chunk(MCIN, nullptr, 256*0x10);
        uint32_t mcinData = mcin + 8;

        uint32_t d, s;
        uint32_t ofsMTEX, ofsMMDX, ofsMMID, ofsMWMO, ofsMWID, ofsMDDF, ofsMODF;
        uint32_t ofsMH2O=0, ofsMFBO=0, ofsMTXF;
        int nTexture = 0;

        // Per-texture state for the ATSC table: the texture name (as the Client stores it, lowercased) and
        // its UV-tiling exponent (MTXF/MTXP bits 4-7). Filled alongside MTEX and MTXF below.
        std::vector<std::string> texNames;
        std::vector<uint8_t>     texScaleExp;
        // Parallel to texNames: modern height-blend params (MTXP heightScale/heightOffset); defaults 0/1.
        std::vector<float>       texHeightScale;
        std::vector<float>       texHeightOffset;
        auto lower = [](std::string s) { for (char& c : s) c = (c == '/') ? '\\' : char(tolower((unsigned char)c)); return s; };

        // MTEX: legacy tiles carry a texture-name table; modern tex0 references textures by FileDataID
        // (MDID) and has no MTEX. When MTEX is absent, resolve each MDID id to a path and synthesize the
        // MTEX blob in id order, so MCLY.textureId stays a valid 0-based index. Height textures (MHID) have
        // no 335 terrain path and are dropped. Without this the target indexes an empty name table -> crash.
        if (hasTex && find(tb, tl, MTEX, d, s) && s > 0)
        {
            ofsMTEX = o.chunk(MTEX, tb+d, s);
            for (uint32_t i=0;i<s;++i) if (tb[d+i]==0) ++nTexture;
            for (uint32_t i=0;i<s;) { const char* n = (const char*)(tb+d+i); texNames.push_back(lower(n)); i += uint32_t(std::strlen(n)) + 1; }
        }
        else if (hasTex && find(tb, tl, MDID, d, s) && s >= 4)
        {
            constexpr const char kFallbackTex[] = "createcrappygreentexture.blp";
            std::vector<uint8_t> blob;
            const uint32_t count = s / 4;
            uint32_t resolved = 0, fallback = 0;
            for (uint32_t i = 0; i < count; ++i)
            {
                const uint32_t fdid = rd32(tb + d + i*4);
                std::string path;
                const bool ok = fdid && rc.resolve && rc.resolve(rc.user, fdid, path) && !path.empty();
                const char* str = ok ? path.c_str() : kFallbackTex;
                (ok ? resolved : fallback) += 1;
                blob.insert(blob.end(), str, str + std::strlen(str));
                blob.push_back(0);
                texNames.push_back(lower(str));
            }
            ofsMTEX = o.chunk(MTEX, blob.data(), uint32_t(blob.size()));
            nTexture = int(count);
            wraith::core::log::Printf("adt: %.*s MDID->MTEX %u textures (resolved=%u fallback=%u)",
                int(name.size()), name.data(), count, resolved, fallback);
        }
        else ofsMTEX = o.chunk(MTEX, nullptr, 0);

        // Object name tables + placements. Legacy obj0 ships MMDX/MMID (+ MWMO/MWID) name tables with
        // index-based placements; modern obj0 has neither and references models by FileDataID. Detect by
        // the name table's presence and rebuild it from the FDIDs when absent.
        const bool legacyNames = hasObj && (find(ob, ol, MMDX, d, s) || find(ob, ol, MWMO, d, s));
        if (legacyNames)
        {
            auto copyObj = [&](uint32_t magic)->uint32_t { uint32_t dd,ss; if (hasObj && find(ob, ol, magic, dd, ss)) return o.chunk(magic, ob+dd, ss); return o.chunk(magic, nullptr, 0); };
            ofsMMDX = copyObj(MMDX); ofsMMID = copyObj(MMID); ofsMWMO = copyObj(MWMO); ofsMWID = copyObj(MWID);

            // MDDF: keep only the target flag bits (0x1/0x2/0x4); scale passes through.
            if (hasObj && find(ob, ol, MDDF, d, s)) {
                ofsMDDF = o.tell(); o.u32(MDDF); o.u32(s); uint32_t b2=o.tell(); o.raw(ob+d, s);
                for (uint32_t e=0;e+36<=s;e+=36) o.patch16(b2+e+0x22, rd16(&o.d[b2+e+0x22]) & 0x7);
            } else ofsMDDF = o.chunk(MDDF, nullptr, 0);

            // MODF: clear flags 0x4/0x8. The modern per-instance scale (0x3E, 1024 = x1.0) is PRESERVED;
            // the target ignores it as padding, and the dynamic_scaling runtime reads it to scale the WMO.
            if (hasObj && find(ob, ol, MODF, d, s)) {
                ofsMODF = o.tell(); o.u32(MODF); o.u32(s); uint32_t b2=o.tell(); o.raw(ob+d, s);
                for (uint32_t e=0;e+64<=s;e+=64) o.patch16(b2+e+0x38, rd16(&o.d[b2+e+0x38]) & ~0xC);
            } else ofsMODF = o.chunk(MODF, nullptr, 0);
        }
        else
        {
            // Modern FDID placements: rebuild MMDX/MMID from MDDF (doodads) and MWMO/MWID from MODF (map
            // objects), resolving each FileDataID to a path. Empty tables when a placement chunk is absent.
            uint32_t md=0, ms=0, od=0, oz=0;
            PlacementRebuild dood, wmo;
            if (hasObj && find(ob, ol, MDDF, md, ms)) dood = rebuildFdidPlacements(ob+md, ms, 36, 0x22, 0x0007, "missing.m2", rc);
            if (hasObj && find(ob, ol, MODF, od, oz))
                // Per-instance scale at 0x3E is PRESERVED (the dynamic_scaling runtime applies it).
                wmo = rebuildFdidPlacements(ob+od, oz, 64, 0x38, uint16_t(~0xC), "missing.wmo", rc);

            ofsMMDX = o.chunk(MMDX, dood.names.data(),   uint32_t(dood.names.size()));
            ofsMMID = o.chunk(MMID, dood.offsets.data(), uint32_t(dood.offsets.size()));
            ofsMWMO = o.chunk(MWMO, wmo.names.data(),     uint32_t(wmo.names.size()));
            ofsMWID = o.chunk(MWID, wmo.offsets.data(),   uint32_t(wmo.offsets.size()));
            ofsMDDF = o.chunk(MDDF, dood.entries.data(),  uint32_t(dood.entries.size()));
            ofsMODF = o.chunk(MODF, wmo.entries.data(),   uint32_t(wmo.entries.size()));

            wraith::core::log::Printf("adt: %.*s FDID placements doodads=%u(miss %u) wmo=%u(miss %u)",
                int(name.size()), name.data(), dood.resolved, dood.missing, wmo.resolved, wmo.missing);
        }

        if (find(rb, rl, MH2O, d, s)) ofsMH2O = o.chunk(MH2O, rb+d, s);

        uint32_t mc[256][2];
        int probeMaxLayers = 0, probeChunksOver4 = 0;
        for (int i = 0; i < 256; ++i)
        {
            uint32_t to=0,tsz=0,oo=0,osz=0;
            if (hasTex && i < tc) { to=tM[i][0]; tsz=tM[i][1]; }
            if (hasObj && i < oc) { oo=oM[i][0]; osz=oM[i][1]; }
            int lc = buildMcnk(o, i, rb, rM[i][0], rM[i][1], tb, to, tsz, ob, oo, osz, mc);
            if (lc > probeMaxLayers) probeMaxLayers = lc;
            if (lc > 4) ++probeChunksOver4;
        }

        if (find(rb, rl, MFBO, d, s)) ofsMFBO = o.chunk(MFBO, rb+d, s);

        // MTXF (texture flags): from tex0 MTXF, else derived from MTXP, else one zero per texture.
        {
            uint32_t md, ms, pd, ps;
            auto rdf = [](const uint8_t* p) { float v; std::memcpy(&v, p, 4); return v; };
            if (hasTex && find(tb, tl, MTXF, md, ms)) {
                // MTXF carries flags only (no height params): scale from bits 4-7, height defaults 0/1.
                ofsMTXF = o.tell(); o.u32(MTXF); o.u32(ms);
                for (uint32_t k=0;k+4<=ms;k+=4) { const uint32_t f = rd32(tb+md+k); o.u32(f & 0x1); texScaleExp.push_back(uint8_t((f >> 4) & 0xF)); texHeightScale.push_back(0.0f); texHeightOffset.push_back(1.0f); }
            } else if (hasTex && find(tb, tl, MTXP, pd, ps)) {
                // MTXP per-texture 0x10: flags u32 @+0, heightScale f @+4, heightOffset f @+8. Synthesize MTXF.
                uint32_t nn = ps/0x10; ofsMTXF = o.tell(); o.u32(MTXF); o.u32(nn*4);
                for (uint32_t k=0;k<nn;++k) { const uint32_t f = rd32(tb+pd+k*0x10); o.u32(f & 0x1); texScaleExp.push_back(uint8_t((f >> 4) & 0xF)); texHeightScale.push_back(rdf(tb+pd+k*0x10+4)); texHeightOffset.push_back(rdf(tb+pd+k*0x10+8)); }
            } else {
                ofsMTXF = o.tell(); o.u32(MTXF); o.u32(uint32_t(nTexture)*4);
                for (int k=0;k<nTexture;++k) { o.u32(0); texHeightScale.push_back(0.0f); texHeightOffset.push_back(1.0f); }
            }
        }

        // Back-patch MHDR offsets (relative to MHDR.data) and flags.
        auto rel = [&](uint32_t abs)->uint32_t { return abs ? abs - mhdrData : 0; };
        o.patch32(mhdrData + 0x00, ofsMFBO ? 0x1 : 0x0); // mhdr_MFBO
        o.patch32(mhdrData + 0x04, rel(mcin));
        o.patch32(mhdrData + 0x08, rel(ofsMTEX));
        o.patch32(mhdrData + 0x0C, rel(ofsMMDX));
        o.patch32(mhdrData + 0x10, rel(ofsMMID));
        o.patch32(mhdrData + 0x14, rel(ofsMWMO));
        o.patch32(mhdrData + 0x18, rel(ofsMWID));
        o.patch32(mhdrData + 0x1C, rel(ofsMDDF));
        o.patch32(mhdrData + 0x20, rel(ofsMODF));
        o.patch32(mhdrData + 0x24, rel(ofsMFBO));
        o.patch32(mhdrData + 0x28, rel(ofsMH2O));
        o.patch32(mhdrData + 0x2C, rel(ofsMTXF));

        // Fill MCIN[256] (offset absolute, size, flags=0, asyncId=0).
        for (int i = 0; i < 256; ++i)
        {
            o.patch32(mcinData + i*0x10 + 0x0, mc[i][0]);
            o.patch32(mcinData + i*0x10 + 0x4, mc[i][1]);
        }

        // Probe: surface tiles whose terrain exceeds the native 4-layer cap (drives the multi-pass work).
        if (probeChunksOver4 > 0)
            wraith::core::log::Printf("adt-probe: %.*s  maxLayers=%d  chunks>4=%d/256",
                static_cast<int>(name.size()), name.data(), probeMaxLayers, probeChunksOver4);

        // ATSC: trailing table of per-texture UV-scale exponents (only the scaled ones). The target navigates
        // the tile via MHDR/MCIN offsets and never reads this trailing chunk; the DLL parses it, accumulates a
        // global name->exponent map, and applies it at terrain draw. Omitted when no texture is scaled.
        {
            std::vector<TexScaleEntry> scales;
            for (size_t k = 0; k < texNames.size(); ++k)
                if (k < texScaleExp.size() && texScaleExp[k] != 0)
                    scales.push_back({ texScaleExp[k], texNames[k] });
            if (!scales.empty())
            {
                std::vector<uint8_t> payload;
                SerializeTexScales(scales, payload);
                o.chunk(kATSC, payload.data(), uint32_t(payload.size()));
                wraith::core::log::Printf("adt: %.*s ATSC %u scaled textures",
                    int(name.size()), name.data(), uint32_t(scales.size()));
            }
        }

        out = std::move(o.d);
        return true;
    }
}
