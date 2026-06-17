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

#include "WmoTranslate.hpp"

#include "WmoChunks.hpp"
#include "WmoPassPlan.hpp"
#include "../ChunkIO.hpp"
#include "Logger.hpp"

#include <cmath>
#include <cstring>
#include <string>

namespace wraith::structure::wmo
{
    namespace
    {
        using iff::Rd16;
        using iff::Rd32;
        using iff::Rdf;
        using iff::Wr16;
        using iff::Wr32;

        // Modern shader id -> target. 0..6 pass through; anything higher collapses to 0 (Diffuse).
        uint32_t RemapShader(uint32_t shader)
        {
            return (shader <= kMaxNativeShader) ? shader : 0;
        }

        // Growable MOTX blob: appends NUL-terminated texture names, dedups, returns the byte offset.
        struct MotxBuilder
        {
            std::vector<uint8_t> data;

            void Seed(const uint8_t* blob, uint32_t len) { data.assign(blob, blob + len); }

            uint32_t Find(const char* path) const
            {
                const size_t pathLen = strlen(path);
                uint32_t i = 0;
                while (i < data.size())
                {
                    const char* s = reinterpret_cast<const char*>(data.data() + i);
                    const size_t sLen = strlen(s);
                    if (sLen == pathLen && memcmp(s, path, pathLen) == 0)
                        return i;
                    i += static_cast<uint32_t>(sLen) + 1;
                }
                return 0xFFFFFFFFu;
            }

            uint32_t Append(const char* path)
            {
                const uint32_t found = Find(path);
                if (found != 0xFFFFFFFFu)
                    return found;
                const uint32_t off = static_cast<uint32_t>(data.size());
                data.insert(data.end(), path, path + strlen(path) + 1);
                return off;
            }
        };

        // Recompute one MOBA i16 bounding box over its vertex range, reading C3Vector from MOVT.
        void FixMobaBox(uint8_t* entry, const uint8_t* movtData, uint32_t movtVerts, uint16_t start, uint16_t end)
        {
            float mn[3] = { 3.4e38f, 3.4e38f, 3.4e38f };
            float mx[3] = { -3.4e38f, -3.4e38f, -3.4e38f };
            bool any = false;

            for (uint32_t i = start; i <= end; ++i)
            {
                if (i >= movtVerts)
                    break;
                const uint8_t* v = movtData + kMovtStride * i;
                for (int k = 0; k < 3; ++k)
                {
                    const float c = Rdf(v + 4 * k);
                    if (c < mn[k]) mn[k] = c;
                    if (c > mx[k]) mx[k] = c;
                }
                any = true;
            }

            if (!any)
                return;

            for (int k = 0; k < 3; ++k)
            {
                Wr16(entry + kMobaBBoxOffset + 2 * k,       static_cast<int16_t>(std::floor(mn[k])));
                Wr16(entry + kMobaBBoxOffset + 2 * (k + 3), static_cast<int16_t>(std::ceil(mx[k])));
            }
        }

        // MOBA material-id relocation: modern builds put the id at +0x0A behind a flag at +0x16; the target
        // reads +0x17. Move it, clear the flag, rebuild the bbox. Returns the count relocated.
        uint32_t FixMobaChunk(uint8_t* mobaData, uint32_t mobaLen, const uint8_t* movtData, uint32_t movtLen)
        {
            const uint32_t n         = mobaLen / kMobaEntryStride;
            const uint32_t movtVerts = movtLen / kMovtStride;
            uint32_t relocated = 0;

            for (uint32_t i = 0; i < n; ++i)
            {
                uint8_t* e = mobaData + i * kMobaEntryStride;
                if ((e[kMobaFlagOffset] & kMobaRelocFlag) == 0)
                    continue;

                e[kMobaMatIdOffset] = e[kMobaModernMatOff];
                e[kMobaFlagOffset]  = 0;

                const uint16_t start = Rd16(e + kMobaMinIndexOff);
                const uint16_t end   = Rd16(e + kMobaMaxIndexOff);
                FixMobaBox(e, movtData, movtVerts, start, end);
                ++relocated;
            }
            return relocated;
        }
    }

    bool TranslateWmoRoot(std::span<const uint8_t> in, const ResolveCtx& rc, std::vector<uint8_t>& out)
    {
        const uint32_t size = static_cast<uint32_t>(in.size());
        const iff::Reader reader(in);

        // Pass 1: locate MOTX/MOMT, detect modern markers and any non-keep chunk.
        iff::Chunk motxC{}, momtC{};
        bool modern = false, strippedUnknown = false;
        reader.ForEach([&](const iff::Chunk& c)
        {
            if (IsRootModernMarker(c.magic)) modern = true;
            else if (c.magic == kMOTX)       motxC = c;
            else if (c.magic == kMOMT)       momtC = c;
            if (!IsRootKeepChunk(c.magic))   strippedUnknown = true;
            return true;
        });

        // Build the final MOTX blob and resolve FileDataID texture references in MOMT against it.
        MotxBuilder motx;
        if (motxC.data)
            motx.Seed(motxC.data, motxC.size);

        // Extra-pass plan: per material, a dropped layer texture to compose at draw time (shipped as WMP1).
        std::vector<PassPlanEntry> plan;
        // Resolve a material texture-slot value to a path: an in-blob MOTX offset reads the name; an
        // out-of-blob value is a FileDataID resolved through the host.
        auto pathOf = [&](uint32_t off) -> std::string
        {
            if (off < motx.data.size())
                return std::string(reinterpret_cast<const char*>(motx.data.data() + off));
            std::string p;
            if (rc.resolve && rc.resolve(rc.user, off, p) && !p.empty())
                return p;
            return std::string();
        };

        std::vector<uint8_t> mats;
        uint32_t matCount = 0, fdidResolved = 0, fdidFallback = 0;
        bool runTimeDirty = false; // a non-zero runTimeData makes the clear a real change
        if (momtC.data)
        {
            mats.assign(momtC.data, momtC.data + momtC.size);
            matCount = momtC.size / kMomtStride;

            uint32_t fallbackOff = 0xFFFFFFFFu; // first resolved tex1 doubles as per-WMO fallback

            uint32_t dbgShader = 0; std::string dbgPath[2]; // probe: what we bind on material 0

            for (uint32_t i = 0; i < matCount; ++i)
            {
                uint8_t* m = mats.data() + i * kMomtStride;
                if (i == 0) dbgShader = Rd32(m + kMomtShaderOffset);

                for (uint32_t b = 0; b < kMomtRunTimeLen; ++b)
                    if (m[kMomtRunTimeOffset + b]) { runTimeDirty = true; break; }
                memset(m + kMomtRunTimeOffset, 0, kMomtRunTimeLen);

                const uint32_t shader = Rd32(m + kMomtShaderOffset);
                if (modern)
                {
                    // The layer slot dropped by the collapse, captured before promotion: shader 23 = tex1
                    // (shine), shader 9 = tex2 (emissive). Both compose additively at draw time.
                    const uint32_t layerOff =
                        (shader == kShaderDFSurface)       ? Rd32(m + kMomtTexOffsets[0]) :
                        (shader == kShaderDiffuseEmissive) ? Rd32(m + kMomtTexOffsets[1]) : 0xFFFFFFFFu;
                    // The base diffuse slot, captured before promotion (shader 23 = tex2, shader 9 = tex1).
                    const uint32_t baseOff =
                        (shader == kShaderDFSurface)       ? Rd32(m + kMomtTexOffsets[1]) :
                        (shader == kShaderDiffuseEmissive) ? Rd32(m + kMomtTexOffsets[0]) : 0xFFFFFFFFu;

                    // Collapse to Diffuse on the real surface texture. Shader 23 keeps its diffuse in tex2
                    // (tex1 is a shared effects map that painted the WMO as a color soup); promote tex2.
                    if (shader == kShaderDFSurface)
                        Wr32(m + kMomtTexOffsets[0], Rd32(m + kMomtTexOffsets[1]));
                    Wr32(m + kMomtShaderOffset, 0);

                    if (layerOff != 0xFFFFFFFFu)
                    {
                        std::string layer = pathOf(layerOff);
                        // Skip a layer identical to the base diffuse (adding a copy of the surface to itself
                        // doubles it to blown-out white). A real shine/emissive is a distinct, additive map.
                        if (!layer.empty() && layer != pathOf(baseOff))
                            plan.push_back({ static_cast<uint16_t>(i), PassBlend::Add, std::move(layer) });
                    }
                }
                else if (shader > kMaxNativeShader)
                    Wr32(m + kMomtShaderOffset, RemapShader(shader));

                for (uint32_t t = 0; t < kMomtTexCount; ++t)
                {
                    const uint32_t off = Rd32(m + kMomtTexOffsets[t]);
                    if (off < motx.data.size())
                        continue; // genuine in-blob offset

                    // Out of MOTX bounds -> a FileDataID; resolve to a path or fall back.
                    std::string path;
                    const bool resolved = rc.resolve && rc.resolve(rc.user, off, path) && !path.empty();
                    uint32_t newOff;
                    if (resolved)
                    {
                        newOff = motx.Append(path.c_str());
                        ++fdidResolved;
                    }
                    else
                    {
                        if (fallbackOff == 0xFFFFFFFFu)
                            fallbackOff = motx.Append(kFallbackTexture);
                        newOff = fallbackOff;
                        ++fdidFallback;
                    }
                    Wr32(m + kMomtTexOffsets[t], newOff);
                    if (i == 0 && t < 2)
                        dbgPath[t] = resolved ? path : std::string("<fdid ") + std::to_string(off) + ">";
                }

                if (fallbackOff == 0xFFFFFFFFu)
                {
                    const uint32_t tex1 = Rd32(m + kMomtTexOffsets[0]);
                    if (tex1 < motx.data.size())
                        fallbackOff = tex1;
                }
            }
            wraith::core::log::Printf("wmo-mat: mats=%u mat0 shader=%u tex1=%s tex2=%s",
                matCount, dbgShader, dbgPath[0].c_str(), dbgPath[1].c_str());
        }

        // A MOTX must always exist (loader base pointer); an empty blob still needs one NUL.
        if (motx.data.empty())
            motx.data.push_back(0);

        // Pure target-shaped root with nothing to change: serve raw.
        const bool createdMotx = (motxC.data == nullptr);
        if (!strippedUnknown && !modern && fdidResolved == 0 && fdidFallback == 0 && !createdMotx && !runTimeDirty)
            return false;

        // Pass 2: rebuild as a whitelist in canonical order. MOTX/MOMT carry rebuilt payloads; missing
        // mandatory chunks are synthesized empty to keep the positional walk aligned; MCVP trails.
        out.clear();
        out.reserve(size + motx.data.size() + 16);

        for (uint32_t magic : kCanonicalRoot)
        {
            if (magic == kMOTX) { iff::Emit(out, kMOTX, motx.data.data(), static_cast<uint32_t>(motx.data.size())); continue; }
            if (magic == kMOMT) { iff::Emit(out, kMOMT, mats.data(), static_cast<uint32_t>(mats.size())); continue; }
            iff::Chunk c{};
            if (reader.Find(magic, c)) iff::Emit(out, magic, c.data, c.size);
            else                       iff::Emit(out, magic, nullptr, 0);
        }

        iff::Chunk mcvp{};
        if (reader.Find(kMCVP, mcvp))
            iff::Emit(out, kMCVP, mcvp.data, mcvp.size);

        // Ship the extra-pass plan as a trailing WMP1 chunk. The DLL reads it and hides it from the loader
        // by shortening the served length, so the native positional walk never sees it.
        if (!plan.empty())
        {
            std::vector<uint8_t> planBytes;
            SerializePassPlan(plan, planBytes);
            iff::Emit(out, kWMP1, planBytes.data(), static_cast<uint32_t>(planBytes.size()));
        }

        return true;
    }

    bool TranslateWmoGroup(std::span<const uint8_t> in, std::vector<uint8_t>& out)
    {
        const uint8_t* buf = in.data();
        const uint32_t size = static_cast<uint32_t>(in.size());

        if (!buf || size < 12 || Rd32(buf) != kMVER)
            return false;

        const uint32_t mverLen   = 8 + Rd32(buf + 4);
        const uint32_t afterMver = mverLen;
        if (afterMver + 8 > size || Rd32(buf + afterMver) != kMOGP)
            return false;

        const uint32_t mogpHdr  = afterMver;
        const uint32_t mogpData = mogpHdr + 8;
        const uint32_t mogpSize = Rd32(buf + mogpHdr + 4);
        const uint32_t mogpEnd  = mogpData + mogpSize;
        if (mogpEnd > size || mogpData + kMogpHeader335 + 8 > mogpEnd)
            return false;

        // Sub-chunks begin after the fixed 0x44 header. A down-convertible group leads with a known chunk
        // (MOPY) or the newer MOGX/MPY2; anything else is left as-is.
        const uint32_t subStart = mogpData + kMogpHeader335;
        if (!IsTranslatableGroupFirst(Rd32(buf + subStart)))
            return false;

        // Walk the sub-chunk region. Keep the first occurrence of each mandatory chunk and the first MOTV /
        // first MOCV; collect optional kept chunks in source order. A 2nd MOTV/MOCV and unknowns are dropped.
        const iff::Reader gr(in.subspan(subStart, mogpEnd - subStart));

        iff::Chunk mopy{}, movi{}, movt{}, monr{}, motv{}, moba{}, mpy2{};
        std::vector<iff::Chunk> kept;
        bool hasMOBN = false, hasMOCV = false, hasMOLR = false, hasMODR = false, hasMLIQ = false;
        bool seenMOTV = false;

        gr.ForEach([&](const iff::Chunk& c)
        {
            switch (c.magic)
            {
                case kMOPY: if (!mopy.data) mopy = c; break;
                case kMPY2: if (!mpy2.data) mpy2 = c; break;
                case kMOVI: if (!movi.data) movi = c; break;
                case kMOVT: if (!movt.data) movt = c; break;
                case kMONR: if (!monr.data) monr = c; break;
                case kMOBA: if (!moba.data) moba = c; break;
                case kMOTV: if (!seenMOTV) { motv = c; seenMOTV = true; } break;
                case kMOCV: if (!hasMOCV) { hasMOCV = true; kept.push_back(c); } break;
                case kMOLR: hasMOLR = true; kept.push_back(c); break;
                case kMODR: hasMODR = true; kept.push_back(c); break;
                case kMOBN: hasMOBN = true; kept.push_back(c); break;
                case kMOBR: kept.push_back(c); break;
                case kMLIQ: hasMLIQ = true; kept.push_back(c); break;
                default: break; // dropped
            }
            return true;
        });

        // The 2nd texcoord/color sets are NOT preserved: the target consumes optional sub-chunks
        // positionally by group flag, so a 2nd set not laid out where it expects misparses the group.
        // Two-layer composition belongs to the multipass module, not the single-pass native path.

        // Vertex colors (MOCV) are emitted verbatim: the native lighting is preserved, never rewritten.

        // Build the rebuilt sub-chunk region: the six mandatory chunks (source bytes, else empty), then
        // the kept optional chunks in source order.
        std::vector<uint8_t> subRegion;
        subRegion.reserve(mogpSize);

        const bool injMOPY = !mopy.data, injMOVI = !movi.data, injMOVT = !movt.data;
        const bool injMONR = !monr.data, injMOTV = !motv.data, injMOBA = !moba.data;

        // Track MOVT/MOBA payload positions inside subRegion for the MOBA fix.
        uint32_t movtPayloadOff = 0, movtPayloadLen = 0;
        uint32_t mobaPayloadOff = 0, mobaPayloadLen = 0;

        auto emitMandatory = [&](const iff::Chunk& c, uint32_t magic, uint32_t* poff, uint32_t* plen)
        {
            const uint32_t payloadOff = static_cast<uint32_t>(subRegion.size()) + 8;
            const uint32_t payloadLen = c.data ? c.size : 0;
            if (c.data) iff::EmitRaw(subRegion, c);
            else        iff::Emit(subRegion, magic, nullptr, 0);
            if (poff) *poff = payloadOff;
            if (plen) *plen = payloadLen;
        };

        // MOPY: from source, or synthesized from MPY2 (4B/tri {u16 flags, u16 mat} -> 2B/tri {u8, u8}).
        // 0xFFFF mat (collision-only) maps to 0xFF; triangle count is identical so MOVI still indexes 1:1.
        std::vector<uint8_t> synthMopy;
        if (!mopy.data && mpy2.data)
        {
            const uint32_t tris = mpy2.size / 4;
            synthMopy.resize(static_cast<size_t>(tris) * 2);
            for (uint32_t t = 0; t < tris; ++t)
            {
                const uint8_t* s = mpy2.data + t * 4;
                const uint16_t fl  = Rd16(s);
                const uint16_t mat = Rd16(s + 2);
                synthMopy[t * 2]     = static_cast<uint8_t>(fl & 0xFF);
                synthMopy[t * 2 + 1] = (mat == 0xFFFF) ? 0xFF : static_cast<uint8_t>(mat & 0xFF);
            }
        }

        if (!synthMopy.empty())
            iff::Emit(subRegion, kMOPY, synthMopy.data(), static_cast<uint32_t>(synthMopy.size()));
        else
            emitMandatory(mopy, kMOPY, nullptr, nullptr);
        emitMandatory(movi, kMOVI, nullptr, nullptr);
        emitMandatory(movt, kMOVT, &movtPayloadOff, &movtPayloadLen);
        emitMandatory(monr, kMONR, nullptr, nullptr);
        emitMandatory(motv, kMOTV, nullptr, nullptr);
        emitMandatory(moba, kMOBA, &mobaPayloadOff, &mobaPayloadLen);

        for (const iff::Chunk& c : kept)
            iff::EmitRaw(subRegion, c);

        // MOBA material-id fix on the freshly emitted MOBA/MOVT payloads. Injected MOBA has no entries.
        if (mobaPayloadLen != 0)
            FixMobaChunk(subRegion.data() + mobaPayloadOff, mobaPayloadLen,
                         subRegion.data() + movtPayloadOff, movtPayloadLen);

        // Assemble: MVER verbatim, MOGP wrapper + fixed 0x44 header (patched), then the rebuilt region.
        out.clear();
        out.reserve(mverLen + 8 + kMogpHeader335 + subRegion.size());

        out.insert(out.end(), buf, buf + mverLen);

        const size_t mogpHeaderStart = out.size();
        out.insert(out.end(), buf + mogpHdr, buf + mogpData + kMogpHeader335);

        uint8_t* outMogpData = out.data() + mogpHeaderStart + 8;

        // Recompute group flags: clear ONLY the flags the target gates chunk consumption on, re-set them for
        // the chunks actually emitted, and preserve every other bit. Clearing an unmanaged bit a native group
        // legitimately sets makes the positional parser misgate and build a corrupt group.
        const uint32_t oldFlags = Rd32(outMogpData + kMogpFlagsOffset);
        uint32_t newFlags = oldFlags & ~kGrpGatedFlags;
        if (hasMOBN) newFlags |= kGrpFlagBSP;
        if (hasMOCV) newFlags |= kGrpFlagMOCV;
        if (hasMOLR) newFlags |= kGrpFlagMOLR;
        if (hasMODR) newFlags |= kGrpFlagMODR;
        if (hasMLIQ) newFlags |= kGrpFlagMLIQ;
        Wr32(outMogpData + kMogpFlagsOffset, newFlags);

        // Injected empty MOBA means zero batches; zero the header counts to match.
        if (injMOBA)
        {
            Wr16(outMogpData + kMogpTransBatchCountOffset, 0);
            Wr16(outMogpData + kMogpIntBatchCountOffset,   0);
            Wr16(outMogpData + kMogpExtBatchCountOffset,   0);
        }

        const uint32_t newMogpSize = kMogpHeader335 + static_cast<uint32_t>(subRegion.size());
        Wr32(out.data() + mogpHeaderStart + 4, newMogpSize);

        out.insert(out.end(), subRegion.begin(), subRegion.end());

        // No-op: nothing injected, no whiten, no flag change, region byte-identical to the source span.
        const bool injected = injMOPY || injMOVI || injMOVT || injMONR || injMOTV || injMOBA;
        const uint32_t origSubLen = mogpEnd - subStart;
        if (!injected && newFlags == oldFlags &&
            subRegion.size() == origSubLen && memcmp(subRegion.data(), buf + subStart, origSubLen) == 0)
            return false;

        return true;
    }
}
