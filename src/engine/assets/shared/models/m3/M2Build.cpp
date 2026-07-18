// Client-model builder implementation: animation bake and model/skin writers.
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

#include "M2Build.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

namespace wxl::modern::assets::m3
{
    namespace
    {
        constexpr char kPlaceholderTex[] = "Interface\\Icons\\INV_Misc_QuestionMark.blp";

        // source blend mode -> client material blend
        constexpr uint16_t kBlendMap[8] = { 0, 2, 4, 4, 5, 6, 4, 4 };

        // client animation ids that loop (0/4/5 also force-looped by the engine)
        const std::set<uint16_t> kLoopIds = { 0, 4, 5, 6, 14, 25, 26, 51, 52, 69 };

        // default movespeed per client animation id (playback rate = unit speed / movespeed)
        float DefaultMoveSpeed(uint16_t id)
        {
            switch (id)
            {
                case 4: case 13: return 2.6f;
                case 5:          return 7.0f;
                default:         return 0.0f;
            }
        }

        constexpr char kDefaultMap[] =
            "Stand:0,Stand 01:0,Fidget:0,Stand S:0,"
            "Walk A:4,Walk A:5,Walk Ready A:13,"
            "Attack:16@0.65,Attack:17@0.65,"
            "Stun:14,Flail:1,Stand Cover:6,Stand Cover End:7,"
            "Flail Back:8,Flail Forward:9,"
            "Spell A:2,Spell B:32,Spell E:33,Spell E Start:31,Spell E End:35,"
            "Spell Omni:54,Spell Forward:53,Spell Channel:51,"
            "Stand Ready:25,Stand Ready:26,Stand Ready End:3,Stand Start Ready:56,"
            "Stand Ready S:52,Stand Ready Start S:34,Stand Ready End S:30,"
            "Stand Cover Start:40,Walk Start:37,"
            "Stand Victory:68,Stand Dance:69,Stand Dance S:69,"
            "Stand Start:66,GLstand:71,"
            "Custom A:60,Custom B:70,Custom C:77,"
            "Taunt:55,Taunt:74";

        constexpr uint32_t kM2Header = 0x130;
        constexpr uint32_t kSkinHeader = 0x30;

        // Append-only image builder; array starts are 16-aligned.
        struct Blob
        {
            std::vector<uint8_t> b;

            void Align() { while (b.size() % 16) b.push_back(0); }

            uint32_t Add(const void* data, size_t n)
            {
                Align();
                const uint32_t off = uint32_t(b.size());
                const uint8_t* p = static_cast<const uint8_t*>(data);
                b.insert(b.end(), p, p + n);
                return off;
            }

            template <typename T>
            void Put(uint32_t off, T v) { std::memcpy(b.data() + off, &v, sizeof(T)); }

            void SetArray(uint32_t field, uint32_t count, uint32_t off)
            {
                Put<uint32_t>(field, count);
                Put<uint32_t>(field + 4, off);
            }
        };

        void AppendU16(std::vector<uint8_t>& v, uint16_t x)
        {
            v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8));
        }

        void AppendU32(std::vector<uint8_t>& v, uint32_t x)
        {
            for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (i * 8)));
        }

        void AppendF(std::vector<uint8_t>& v, float f)
        {
            uint32_t x; std::memcpy(&x, &f, 4); AppendU32(v, x);
        }

        // compressed quaternion component: int16 scale shifted into u16
        uint16_t CompQuat(float f)
        {
            int v = int(std::lround(f * 32767.0f));
            v = std::clamp(v, -32767, 32767);
            return uint16_t(v + 32767);
        }

        /** @brief 20-byte track header pointing at per-sequence key arrays. */
        struct TrackRef { uint16_t interp; int16_t gseq; uint32_t tsN, tsOfs, vaN, vaOfs; };

        void AppendTrack(std::vector<uint8_t>& rec, const TrackRef& t)
        {
            AppendU16(rec, t.interp);
            AppendU16(rec, uint16_t(t.gseq));
            AppendU32(rec, t.tsN); AppendU32(rec, t.tsOfs);
            AppendU32(rec, t.vaN); AppendU32(rec, t.vaOfs);
        }

        /** @brief Static per-sequence track: one key [0] shared by every sequence. */
        TrackRef StaticTrack(Blob& blob, const void* value, uint32_t valueSize, uint32_t nseq)
        {
            const uint32_t n = nseq ? nseq : 1;
            uint32_t zero = 0;
            const uint32_t tOff = blob.Add(&zero, 4);
            const uint32_t vOff = blob.Add(value, valueSize);
            std::vector<uint8_t> outer;
            for (uint32_t i = 0; i < n; ++i) { AppendU32(outer, 1); AppendU32(outer, tOff); }
            const uint32_t ts = blob.Add(outer.data(), outer.size());
            outer.clear();
            for (uint32_t i = 0; i < n; ++i) { AppendU32(outer, 1); AppendU32(outer, vOff); }
            const uint32_t va = blob.Add(outer.data(), outer.size());
            return { 0, -1, n, ts, n, va };
        }

        TrackRef StaticFloatTrack(Blob& blob, float value, uint32_t nseq)
        {
            return StaticTrack(blob, &value, 4, nseq);
        }

        /** @brief Per-sequence float track from a bake, one static fallback key per
         *  sequence missing a library track (e.g. particle rate keyed to gait). */
        TrackRef BakedOrStaticFloatTrack(Blob& blob, const BakeResult& bake, size_t index,
                                         float fallback, uint32_t nseq)
        {
            const bool hasAny = index < bake.emitterRate.size() &&
                std::any_of(bake.emitterRate[index].begin(), bake.emitterRate[index].end(),
                            [](const auto& k) { return k.has_value(); });
            if (!hasAny) return StaticFloatTrack(blob, fallback, nseq);

            const auto& per = bake.emitterRate[index];
            std::vector<uint8_t> outer_ts, outer_va;
            for (uint32_t si = 0; si < nseq; ++si)
            {
                if (si >= per.size() || !per[si])
                {
                    uint32_t zero = 0;
                    const uint32_t tOff = blob.Add(&zero, 4);
                    const uint32_t vOff = blob.Add(&fallback, 4);
                    AppendU32(outer_ts, 1); AppendU32(outer_ts, tOff);
                    AppendU32(outer_va, 1); AppendU32(outer_va, vOff);
                    continue;
                }
                const BakedScalar& keys = *per[si];
                std::vector<uint8_t> ts, va;
                for (size_t k = 0; k < keys.times.size(); ++k)
                {
                    AppendU32(ts, keys.times[k]);
                    AppendF(va, keys.values[k]);
                }
                const uint32_t tOff = blob.Add(ts.data(), ts.size());
                const uint32_t vOff = blob.Add(va.data(), va.size());
                AppendU32(outer_ts, uint32_t(keys.times.size())); AppendU32(outer_ts, tOff);
                AppendU32(outer_va, uint32_t(keys.times.size())); AppendU32(outer_va, vOff);
            }
            const uint32_t tsTab = blob.Add(outer_ts.data(), outer_ts.size());
            const uint32_t vaTab = blob.Add(outer_va.data(), outer_va.size());
            return { 1, -1, nseq, tsTab, nseq, vaTab };
        }

        /** @brief FBlock (fixed16-timeline track): 16 bytes of {timestamps, values} arrays. */
        void AppendFBlock(std::vector<uint8_t>& rec, Blob& blob,
                          const std::vector<uint16_t>& times, const std::vector<uint8_t>& values,
                          uint32_t valueCount)
        {
            std::vector<uint8_t> ts;
            for (uint16_t t : times) AppendU16(ts, t);
            const uint32_t tOff = blob.Add(ts.data(), ts.size());
            const uint32_t vOff = blob.Add(values.data(), values.size());
            AppendU32(rec, uint32_t(times.size())); AppendU32(rec, tOff);
            AppendU32(rec, valueCount); AppendU32(rec, vOff);
        }

        // distinct sub-unit batch alphas, in first-appearance order; the model
        // colors array and the skin color indices both derive from this list
        std::vector<float> BatchAlphas(const Model& model)
        {
            std::vector<float> v;
            for (const Batch& b : model.batches)
                if (b.alpha < 1.0f &&
                    std::find(v.begin(), v.end(), b.alpha) == v.end())
                    v.push_back(b.alpha);
            return v;
        }

        std::vector<uint16_t> ThreePhase(float mid)
        {
            const uint16_t m = uint16_t(std::clamp(int(mid * 32767.0f), 1, 32766));
            return { 0, m, 32767 };
        }

        /** @brief Closed hash table for the sequence lookup: -1 marks the empty slots. */
        std::vector<int16_t> SeqHashTable(const std::vector<std::pair<uint16_t, uint16_t>>& entries)
        {
            const uint32_t n = std::max<uint32_t>(2, uint32_t(entries.size()) * 2);
            std::vector<int16_t> table(n, -1);
            for (const auto& [id, si] : entries)
            {
                uint32_t i = id % n;
                while (table[i] != -1) i = (i + 1) % n;
                table[i] = int16_t(si);
            }
            return table;
        }

        LibTrack ResolveLayered(const AnimLib& lib, const std::vector<uint32_t>& stcs,
                                uint32_t animId, uint32_t slot, uint32_t width)
        {
            LibTrack t;
            for (uint32_t s : stcs)
                if (lib.Track(s, animId, slot, width, t)) return t;
            return {};
        }

        Vec3 SampleV3(const LibTrack& t, int32_t time, const Vec3& dflt)
        {
            if (!t.count) return dflt;
            const auto val = [&](uint32_t k) {
                Vec3 v; std::memcpy(v.data(), t.values + k * t.width, 12); return v;
            };
            if (time <= t.times[0]) return val(0);
            if (time >= t.times[t.count - 1]) return val(t.count - 1);
            uint32_t lo = 0, hi = t.count - 1;
            while (hi - lo > 1)
            {
                const uint32_t mid = (lo + hi) / 2;
                if (t.times[mid] <= time) lo = mid; else hi = mid;
            }
            const int32_t span = t.times[hi] - t.times[lo];
            const float f = span ? float(time - t.times[lo]) / float(span) : 0.0f;
            return LerpV3(val(lo), val(hi), f);
        }

        float SampleF(const LibTrack& t, int32_t time, float dflt)
        {
            if (!t.count) return dflt;
            const auto val = [&](uint32_t k) {
                float v; std::memcpy(&v, t.values + k * t.width, 4); return v;
            };
            if (time <= t.times[0]) return val(0);
            if (time >= t.times[t.count - 1]) return val(t.count - 1);
            uint32_t lo = 0, hi = t.count - 1;
            while (hi - lo > 1)
            {
                const uint32_t mid = (lo + hi) / 2;
                if (t.times[mid] <= time) lo = mid; else hi = mid;
            }
            const int32_t span = t.times[hi] - t.times[lo];
            const float f = span ? float(time - t.times[lo]) / float(span) : 0.0f;
            return val(lo) + (val(hi) - val(lo)) * f;
        }

        Quat SampleQ(const LibTrack& t, int32_t time, const Quat& dflt)
        {
            if (!t.count) return dflt;
            const auto val = [&](uint32_t k) {
                Quat q; std::memcpy(q.data(), t.values + k * t.width, 16); return q;
            };
            if (time <= t.times[0]) return val(0);
            if (time >= t.times[t.count - 1]) return val(t.count - 1);
            uint32_t lo = 0, hi = t.count - 1;
            while (hi - lo > 1)
            {
                const uint32_t mid = (lo + hi) / 2;
                if (t.times[mid] <= time) lo = mid; else hi = mid;
            }
            const int32_t span = t.times[hi] - t.times[lo];
            const float f = span ? float(time - t.times[lo]) / float(span) : 0.0f;
            return NlerpQ(val(lo), val(hi), f);
        }
    }

    std::vector<SeqMap> ParseSeqMap(const std::string& mapping)
    {
        const std::string& src = mapping.empty() ? std::string(kDefaultMap) : mapping;
        std::vector<SeqMap> out;
        size_t pos = 0;
        while (pos < src.size())
        {
            size_t end = src.find(',', pos);
            if (end == std::string::npos) end = src.size();
            const std::string part = src.substr(pos, end - pos);
            pos = end + 1;
            const size_t colon = part.rfind(':');
            if (colon == std::string::npos) continue;
            SeqMap e{ part.substr(0, colon), 0, 1.0f, -1.0f };
            std::string rest = part.substr(colon + 1);
            const size_t a1 = rest.find('@');
            if (a1 != std::string::npos)
            {
                const size_t a2 = rest.find('@', a1 + 1);
                if (a2 != std::string::npos)
                {
                    e.speed = std::stof(rest.substr(a2 + 1));
                    rest = rest.substr(0, a2);
                }
                if (a1 + 1 < rest.size()) e.tscale = std::stof(rest.substr(a1 + 1));
                rest = rest.substr(0, a1);
            }
            e.wowId = uint16_t(std::stoul(rest));
            out.push_back(std::move(e));
        }
        return out;
    }

    bool BakeSequences(const Model& model, const AnimLib& lib, const std::vector<SeqMap>& map,
                       const Mat4& frame, BakeResult& out)
    {
        const size_t nbones = model.bones.size();
        std::vector<Mat4> rest(nbones); // absolute rest = inverse(iref)
        out.pivots.resize(nbones);
        for (size_t i = 0; i < nbones; ++i)
        {
            rest[i] = MatInverse(model.irefs[i]);
            out.pivots[i] = { rest[i].m[3][0], rest[i].m[3][1], rest[i].m[3][2] };
        }

        struct Picked { const LibSeq* src; std::vector<uint32_t> stcs; float tscale; };
        std::vector<Picked> picked;
        for (const SeqMap& e : map)
        {
            const LibSeq* src = nullptr;
            for (const LibSeq& s : lib.seqs)
                if (s.name == e.name) { src = &s; break; }
            if (!src) continue;
            // primary (non-concurrent) collections first, overlay layers fill the gaps
            std::vector<uint32_t> layers = src->stcs;
            std::stable_sort(layers.begin(), layers.end(), [&](uint32_t a, uint32_t b) {
                const uint16_t ca = a < lib.stcs.size() ? lib.stcs[a].conc : 0;
                const uint16_t cb = b < lib.stcs.size() ? lib.stcs[b].conc : 0;
                return ca < cb;
            });
            out.seqs.push_back({ e.wowId,
                                 uint32_t(std::lround((src->end - src->start) * e.tscale)),
                                 e.speed });
            picked.push_back({ src, std::move(layers), e.tscale });
        }
        if (picked.empty()) return false;

        out.tracks.assign(nbones, {});
        for (size_t bi = 0; bi < nbones; ++bi)
        {
            const BoneRec& b = model.bones[bi];
            const LibBone* ids = nullptr;
            const auto it = lib.bones.find(b.name);
            if (it != lib.bones.end()) ids = &it->second;

            const Mat4& biInv = model.irefs[bi];
            const Mat4  bpar = (b.parent >= 0) ? rest[b.parent] : frame;
            const Mat4  tp   = Translation(out.pivots[bi]);
            const Mat4  tpn  = Translation({ -out.pivots[bi][0], -out.pivots[bi][1],
                                             -out.pivots[bi][2] });

            auto& per = out.tracks[bi];
            per.resize(picked.size());
            for (size_t si = 0; si < picked.size(); ++si)
            {
                const Picked& p = picked[si];
                LibTrack locT, rotT, sclT;
                if (ids)
                {
                    locT = ResolveLayered(lib, p.stcs, ids->loc, 2, 12);
                    rotT = ResolveLayered(lib, p.stcs, ids->rot, 3, 16);
                    sclT = ResolveLayered(lib, p.stcs, ids->scale, 2, 12);
                }
                if (!locT.count && !rotT.count && !sclT.count) continue;

                const int32_t dur = int32_t(p.src->end - p.src->start);
                std::set<int32_t> timeSet = { 0, dur };
                for (const LibTrack* t : { &locT, &rotT, &sclT })
                    for (uint32_t k = 0; k < t->count; ++k)
                        if (t->times[k] >= 0 && t->times[k] <= dur) timeSet.insert(t->times[k]);

                BakedKeys keys;
                for (int32_t t : timeSet)
                {
                    const Vec3 loc = SampleV3(locT, t, b.initLoc);
                    const Quat rot = SampleQ(rotT, t, b.initRot);
                    const Vec3 scl = SampleV3(sclT, t, b.initScale);
                    const Mat4 L = ComposeSRT(scl, rot, loc);
                    const Mat4 Z = MatMul(MatMul(MatMul(tp, biInv), MatMul(L, bpar)), tpn);
                    Vec3 s, tr; Quat q;
                    DecomposeSRT(Z, s, q, tr);
                    // hemisphere continuity: the client lerps stored keys directly and a
                    // sign flip between neighbours swings through zero
                    if (!keys.rots.empty())
                    {
                        const Quat& prev = keys.rots.back();
                        if (prev[0] * q[0] + prev[1] * q[1] + prev[2] * q[2] + prev[3] * q[3] < 0)
                            for (auto& c : q) c = -c;
                    }
                    keys.times.push_back(uint32_t(std::lround(t * p.tscale)));
                    keys.locs.push_back(tr);
                    keys.rots.push_back(q);
                    keys.scales.push_back(s);
                }
                per[si] = std::move(keys);
            }
        }

        // particle rate: some emitters are burst-driven (e.g. footstep dust keyed to the
        // gait cycle) rather than a constant stream; the SD slot is empirically slot 5,
        // stride 4 (plain scalar float) -- confirmed by clean values on bear_rank17's paw
        // emitters (0 most of the cycle, spiking during the footfall window)
        constexpr uint32_t kSdSlotScalar = 5;
        out.emitterRate.assign(model.particles.size(), {});
        for (size_t ei = 0; ei < model.particles.size(); ++ei)
        {
            const Emitter& em = model.particles[ei];
            if (em.rateAnimId == 0xFFFFFFFF) continue;
            auto& per = out.emitterRate[ei];
            per.resize(picked.size());
            for (size_t si = 0; si < picked.size(); ++si)
            {
                const Picked& p = picked[si];
                const LibTrack rt = ResolveLayered(lib, p.stcs, em.rateAnimId, kSdSlotScalar, 4);
                if (!rt.count) continue;

                const int32_t dur = int32_t(p.src->end - p.src->start);
                std::set<int32_t> timeSet = { 0, dur };
                for (uint32_t k = 0; k < rt.count; ++k)
                    if (rt.times[k] >= 0 && rt.times[k] <= dur) timeSet.insert(rt.times[k]);

                BakedScalar keys;
                for (int32_t t : timeSet)
                {
                    keys.times.push_back(uint32_t(std::lround(t * p.tscale)));
                    keys.values.push_back(SampleF(rt, t, em.rate));
                }
                per[si] = std::move(keys);
            }
        }
        return true;
    }

    bool OverlayAmbient(const Model& model, const AnimLib& self, const std::string& seqName,
                        const Mat4& frame, BakeResult& out)
    {
        const LibSeq* src = nullptr;
        for (const LibSeq& s : self.seqs)
            if (s.name == seqName) { src = &s; break; }
        if (!src || out.tracks.size() != model.bones.size() || out.seqs.empty())
            return false;

        std::vector<uint32_t> layers = src->stcs;
        std::stable_sort(layers.begin(), layers.end(), [&](uint32_t a, uint32_t b) {
            const uint16_t ca = a < self.stcs.size() ? self.stcs[a].conc : 0;
            const uint16_t cb = b < self.stcs.size() ? self.stcs[b].conc : 0;
            return ca < cb;
        });

        const size_t nseq = out.seqs.size();
        bool any = false;
        for (size_t bi = 0; bi < model.bones.size(); ++bi)
        {
            auto& per = out.tracks[bi];
            if (per.size() < nseq) per.resize(nseq);
            const bool animated = std::any_of(per.begin(), per.end(),
                                              [](const auto& k) { return k.has_value(); });
            if (animated) continue;

            const BoneRec& b = model.bones[bi];
            const auto it = self.bones.find(b.name);
            if (it == self.bones.end()) continue;
            const LibTrack locT = ResolveLayered(self, layers, it->second.loc, 2, 12);
            const LibTrack rotT = ResolveLayered(self, layers, it->second.rot, 3, 16);
            const LibTrack sclT = ResolveLayered(self, layers, it->second.scale, 2, 12);
            if (!locT.count && !rotT.count && !sclT.count) continue;

            const Mat4& biInv = model.irefs[bi];
            const Mat4  bpar = (b.parent >= 0) ? MatInverse(model.irefs[b.parent]) : frame;
            const Mat4  tp   = Translation(out.pivots[bi]);
            const Mat4  tpn  = Translation({ -out.pivots[bi][0], -out.pivots[bi][1],
                                             -out.pivots[bi][2] });

            const int32_t dur = int32_t(src->end - src->start);
            std::set<int32_t> timeSet = { 0, dur };
            for (const LibTrack* t : { &locT, &rotT, &sclT })
                for (uint32_t k = 0; k < t->count; ++k)
                    if (t->times[k] >= 0 && t->times[k] <= dur) timeSet.insert(t->times[k]);

            BakedKeys keys;
            for (int32_t t : timeSet)
            {
                const Vec3 loc = SampleV3(locT, t, b.initLoc);
                const Quat rot = SampleQ(rotT, t, b.initRot);
                const Vec3 scl = SampleV3(sclT, t, b.initScale);
                const Mat4 L = ComposeSRT(scl, rot, loc);
                const Mat4 Z = MatMul(MatMul(MatMul(tp, biInv), MatMul(L, bpar)), tpn);
                Vec3 s, tr; Quat q;
                DecomposeSRT(Z, s, q, tr);
                if (!keys.rots.empty())
                {
                    const Quat& prev = keys.rots.back();
                    if (prev[0] * q[0] + prev[1] * q[1] + prev[2] * q[2] + prev[3] * q[3] < 0)
                        for (auto& c : q) c = -c;
                }
                keys.times.push_back(uint32_t(t));
                keys.locs.push_back(tr);
                keys.rots.push_back(q);
                keys.scales.push_back(s);
            }
            for (size_t si = 0; si < nseq; ++si) per[si] = keys;
            any = true;
        }
        return any;
    }

    void StaticBake(const Model& model, BakeResult& out)
    {
        out.seqs.push_back({ 0, 1000, 0.0f });
        out.tracks.assign(model.bones.size(), std::vector<std::optional<BakedKeys>>(1));
        out.pivots.resize(model.bones.size());
        for (size_t i = 0; i < model.bones.size(); ++i)
        {
            const Mat4 b = MatInverse(model.irefs[i]);
            out.pivots[i] = { b.m[3][0], b.m[3][1], b.m[3][2] };
        }
    }

    void BuildM2(const Model& model, const std::string& name, const std::string& texMode,
                 const BakeResult& bake, const float* tint, std::vector<uint8_t>& out,
                 std::vector<std::string>* fxTextures)
    {
        const uint32_t nseq = uint32_t(bake.seqs.size());
        Blob blob;
        blob.b.assign(kM2Header, 0);

        // name (count includes the terminator)
        {
            std::vector<uint8_t> n(name.begin(), name.end());
            n.push_back(0);
            blob.SetArray(0x08, uint32_t(n.size()), blob.Add(n.data(), n.size()));
        }

        // sequences: same-id entries chain as variations with split frequency
        std::map<uint16_t, std::vector<uint32_t>> groups;
        for (uint32_t i = 0; i < nseq; ++i)
            groups[bake.seqs[i].id].push_back(i);
        {
            std::vector<uint8_t> data;
            for (uint32_t i = 0; i < nseq; ++i)
            {
                const BakedSeq& s = bake.seqs[i];
                const auto& g = groups[s.id];
                const uint32_t vi = uint32_t(std::find(g.begin(), g.end(), i) - g.begin());
                const int16_t vnext = (vi + 1 < g.size()) ? int16_t(g[vi + 1]) : int16_t(-1);
                const uint16_t freq = (vi == 0)
                    ? uint16_t(32767 - 5000 * (g.size() - 1)) : uint16_t(5000);
                const uint32_t flags = 0x20 | (kLoopIds.count(s.id) ? 0 : 1);
                const float speed = (s.speed >= 0.0f) ? s.speed : DefaultMoveSpeed(s.id);

                AppendU16(data, s.id); AppendU16(data, uint16_t(vi));
                AppendU32(data, s.duration); AppendF(data, speed);
                AppendU32(data, flags);
                AppendU16(data, uint16_t(32767)); AppendU16(data, 0);
                AppendU32(data, 0); AppendU32(data, 0); AppendU32(data, 150);
                for (float f : model.bmin) AppendF(data, f);
                for (float f : model.bmax) AppendF(data, f);
                AppendF(data, model.radius);
                AppendU16(data, uint16_t(vnext)); AppendU16(data, uint16_t(i));
            }
            blob.SetArray(0x1C, nseq, blob.Add(data.data(), data.size()));
        }
        {
            std::vector<std::pair<uint16_t, uint16_t>> firsts;
            for (const auto& [id, g] : groups) firsts.emplace_back(id, uint16_t(g[0]));
            const auto table = SeqHashTable(firsts);
            blob.SetArray(0x24, uint32_t(table.size()),
                          blob.Add(table.data(), table.size() * 2));
        }

        // emitter bones carry the emission axis in their rest rotation, which the
        // bake conjugates to identity for skinning; reinstate it as a static local
        // rotation (plus the record's tilt angles) so particles leave along the
        // source direction. Emitter bones are leaf SFX bones with no skinned verts.
        std::map<uint32_t, Quat> emitRot;
        for (const Emitter& p : model.particles)
        {
            if (p.bone >= model.irefs.size()) continue;
            Mat4 rot = MatInverse(model.irefs[p.bone]);
            rot.m[3][0] = rot.m[3][1] = rot.m[3][2] = 0;
            const float ax = p.angle[0] * 3.14159265f / 180.0f;
            const float ay = p.angle[1] * 3.14159265f / 180.0f;
            Mat4 tilt = MatIdentity();
            tilt.m[1][1] = std::cos(ax); tilt.m[1][2] = std::sin(ax);
            tilt.m[2][1] = -std::sin(ax); tilt.m[2][2] = std::cos(ax);
            Mat4 tiltY = MatIdentity();
            tiltY.m[0][0] = std::cos(ay); tiltY.m[0][2] = -std::sin(ay);
            tiltY.m[2][0] = std::sin(ay); tiltY.m[2][2] = std::cos(ay);
            emitRot[p.bone] = QuatFromMat(MatMul(MatMul(tilt, tiltY), rot));
        }

        // bones: flags 0x200 (transformed) or the engine never stores the animated TRS
        {
            std::vector<uint8_t> data;
            for (size_t bi = 0; bi < model.bones.size(); ++bi)
            {
                AppendU32(data, uint32_t(-1));      // keyBoneId
                AppendU32(data, 0x200);             // flags
                AppendU16(data, uint16_t(model.bones[bi].parent));
                AppendU16(data, 0);                 // submeshId
                AppendU32(data, 0);

                const auto& per = bake.tracks[bi];
                const bool animated = std::any_of(per.begin(), per.end(),
                                                  [](const auto& k) { return k.has_value(); });
                const auto er = emitRot.find(uint32_t(bi));
                for (int channel = 0; channel < 3; ++channel)
                {
                    if (!animated)
                    {
                        if (channel == 1 && er != emitRot.end())
                        {
                            uint16_t cq[4];
                            for (int a = 0; a < 4; ++a) cq[a] = CompQuat(er->second[a]);
                            AppendTrack(data, StaticTrack(blob, cq, 8, nseq));
                        }
                        else
                            AppendTrack(data, { 0, -1, 0, 0, 0, 0 });
                        continue;
                    }
                    std::vector<uint8_t> outer_ts, outer_va;
                    for (const auto& keys : per)
                    {
                        if (!keys)
                        {
                            AppendU32(outer_ts, 0); AppendU32(outer_ts, 0);
                            AppendU32(outer_va, 0); AppendU32(outer_va, 0);
                            continue;
                        }
                        std::vector<uint8_t> ts, va;
                        for (size_t k = 0; k < keys->times.size(); ++k)
                        {
                            AppendU32(ts, keys->times[k]);
                            if (channel == 0)
                                for (float f : keys->locs[k]) AppendF(va, f);
                            else if (channel == 1)
                            {
                                Quat q = keys->rots[k];
                                if (er != emitRot.end())
                                    q = QuatFromMat(MatMul(MatFromQuat(q),
                                                           MatFromQuat(er->second)));
                                for (float f : q) AppendU16(va, CompQuat(f));
                            }
                            else
                                for (float f : keys->scales[k]) AppendF(va, f);
                        }
                        const uint32_t tOff = blob.Add(ts.data(), ts.size());
                        const uint32_t vOff = blob.Add(va.data(), va.size());
                        AppendU32(outer_ts, uint32_t(keys->times.size())); AppendU32(outer_ts, tOff);
                        AppendU32(outer_va, uint32_t(keys->times.size())); AppendU32(outer_va, vOff);
                    }
                    const uint32_t tsTab = blob.Add(outer_ts.data(), outer_ts.size());
                    const uint32_t vaTab = blob.Add(outer_va.data(), outer_va.size());
                    AppendTrack(data, { 1, -1, uint32_t(per.size()), tsTab,
                                        uint32_t(per.size()), vaTab });
                }
                for (float f : bake.pivots[bi]) AppendF(data, f);
            }
            blob.SetArray(0x2C, uint32_t(model.bones.size()),
                          blob.Add(data.data(), data.size()));
        }
        {
            const int16_t none = -1;
            blob.SetArray(0x34, 1, blob.Add(&none, 2)); // keyBoneLookup
        }

        // batch colors: static white with the sub-unit alpha of each fade band
        {
            const std::vector<float> alphas = BatchAlphas(model);
            if (!alphas.empty())
            {
                std::vector<uint8_t> cols;
                for (float a : alphas)
                {
                    const float white[3] = { 1.0f, 1.0f, 1.0f };
                    AppendTrack(cols, StaticTrack(blob, white, 12, nseq));
                    const uint16_t av = uint16_t(a * 32767.0f);
                    AppendTrack(cols, StaticTrack(blob, &av, 2, nseq));
                }
                blob.SetArray(0x48, uint32_t(alphas.size()),
                              blob.Add(cols.data(), cols.size()));
            }
        }

        // vertices: weights normalized to 255, global bone indices via the region lookup
        {
            std::vector<uint8_t> data;
            for (size_t vi = 0; vi < model.verts.size(); ++vi)
            {
                const Vertex& v = model.verts[vi];
                const Region& r = model.regions[model.vertRegion[vi]];
                int w[4] = { v.w[0], v.w[1], v.w[2], v.w[3] };
                const int tot = w[0] + w[1] + w[2] + w[3];
                if (tot == 0) { w[0] = 255; w[1] = w[2] = w[3] = 0; }
                else if (tot != 255)
                {
                    int acc = 0;
                    for (int k = 0; k < 4; ++k) { w[k] = w[k] * 255 / tot; acc += w[k]; }
                    w[0] += 255 - acc;
                }
                for (float f : v.pos) AppendF(data, f);
                for (int k = 0; k < 4; ++k) data.push_back(uint8_t(w[k]));
                for (int k = 0; k < 4; ++k)
                {
                    const uint32_t li = uint32_t(r.firstLookup) + v.bl[k];
                    data.push_back(uint8_t((w[k] && li < model.lookup.size())
                                           ? model.lookup[li] : 0));
                }
                for (float f : v.normal) AppendF(data, f);
                AppendF(data, v.uv0[0]); AppendF(data, v.uv0[1]);
                AppendF(data, v.uv1[0]); AppendF(data, v.uv1[1]);
            }
            blob.SetArray(0x3C, uint32_t(model.verts.size()),
                          blob.Add(data.data(), data.size()));
        }
        blob.Put<uint32_t>(0x44, 1); // numSkinProfiles

        // textures: one per material plus one per distinct particle atlas.
        // "@skin:<prefix>" routes opaque diffuse materials through creature-skin
        // types 11/12/13 (display-row texture variations); blended FX materials
        // and particle atlases keep hardcoded paths under the prefix.
        std::vector<Material> materials = model.materials;
        if (materials.empty()) materials.push_back({ 0, "" });
        std::vector<uint16_t> parTex(model.particles.size(), 0);
        std::vector<uint16_t> ribTex(model.ribbons.size(), 0);
        {
            const bool skinMode = texMode.rfind("@skin", 0) == 0;
            std::string prefix = texMode;
            if (skinMode)
            {
                const size_t colon = texMode.find(':');
                prefix = (colon != std::string::npos) ? texMode.substr(colon + 1) : "";
            }
            const auto pathFor = [&](const std::string& img)
            {
                std::string base = img;
                std::replace(base.begin(), base.end(), '/', '\\');
                const size_t sep = base.rfind('\\');
                if (sep != std::string::npos) base = base.substr(sep + 1);
                const size_t dot = base.rfind('.');
                if (dot != std::string::npos) base = base.substr(0, dot);
                std::transform(base.begin(), base.end(), base.begin(),
                               [](unsigned char c) { return char(std::tolower(c)); });
                return prefix + base + ".blp";
            };
            struct TexRec { uint32_t type, n, ofs; };
            std::vector<TexRec> recs;
            uint32_t slot = 0;
            for (const Material& m : materials)
            {
                if (!m.diffuse.empty() && skinMode && m.blend == 0)
                {
                    recs.push_back({ 11 + std::min(slot, 2u), 0, 0 });
                    ++slot;
                    continue;
                }
                std::string path = kPlaceholderTex;
                if (!m.diffuse.empty() && !prefix.empty())
                    path = pathFor(m.diffuse);
                std::vector<uint8_t> n(path.begin(), path.end());
                n.push_back(0);
                recs.push_back({ 0, uint32_t(n.size()), blob.Add(n.data(), n.size()) });
            }
            // particle atlases as extra entries past the material block so
            // material/texture indices stay 1:1 for the geometry batches
            std::vector<std::pair<std::string, uint16_t>> atlasIndex;
            for (size_t i = 0; i < model.particles.size(); ++i)
            {
                const Emitter& p = model.particles[i];
                parTex[i] = uint16_t(p.mat < materials.size() ? p.mat : 0);
                if (p.image.empty() || prefix.empty()) continue;
                const std::string path = pathFor(p.image);
                bool found = false;
                for (const auto& a : atlasIndex)
                    if (a.first == path) { parTex[i] = a.second; found = true; break; }
                if (found) continue;
                std::vector<uint8_t> n(path.begin(), path.end());
                n.push_back(0);
                recs.push_back({ 0, uint32_t(n.size()), blob.Add(n.data(), n.size()) });
                parTex[i] = uint16_t(recs.size() - 1);
                atlasIndex.emplace_back(path, parTex[i]);
                if (fxTextures) fxTextures->push_back(path);
            }
            for (size_t i = 0; i < model.ribbons.size(); ++i)
            {
                const Ribbon& r = model.ribbons[i];
                ribTex[i] = uint16_t(r.mat < materials.size() ? r.mat : 0);
                if (r.image.empty() || prefix.empty()) continue;
                const std::string path = pathFor(r.image);
                bool found = false;
                for (const auto& a : atlasIndex)
                    if (a.first == path) { ribTex[i] = a.second; found = true; break; }
                if (found) continue;
                std::vector<uint8_t> n(path.begin(), path.end());
                n.push_back(0);
                recs.push_back({ 0, uint32_t(n.size()), blob.Add(n.data(), n.size()) });
                ribTex[i] = uint16_t(recs.size() - 1);
                atlasIndex.emplace_back(path, ribTex[i]);
                if (fxTextures) fxTextures->push_back(path);
            }
            uint32_t arrOff = 0;
            for (size_t i = 0; i < recs.size(); ++i)
            {
                uint8_t rec[16];
                std::memcpy(rec + 0, &recs[i].type, 4);
                const uint32_t wrap = 3;
                std::memcpy(rec + 4, &wrap, 4);
                std::memcpy(rec + 8, &recs[i].n, 4);
                std::memcpy(rec + 12, &recs[i].ofs, 4);
                const uint32_t off = blob.Add(rec, 16);
                if (i == 0) arrOff = off;
            }
            blob.SetArray(0x50, uint32_t(recs.size()), arrOff);
        }

        // one static full-opacity texture weight
        {
            const uint32_t n = nseq ? nseq : 1;
            uint32_t zero = 0;
            const uint16_t full = 0x7FFF;
            const uint32_t tOff = blob.Add(&zero, 4);
            const uint32_t vOff = blob.Add(&full, 2);
            std::vector<uint8_t> outer;
            for (uint32_t i = 0; i < n; ++i) { AppendU32(outer, 1); AppendU32(outer, tOff); }
            const uint32_t ts = blob.Add(outer.data(), outer.size());
            outer.clear();
            for (uint32_t i = 0; i < n; ++i) { AppendU32(outer, 1); AppendU32(outer, vOff); }
            const uint32_t va = blob.Add(outer.data(), outer.size());
            std::vector<uint8_t> w;
            AppendTrack(w, { 0, -1, n, ts, n, va });
            blob.SetArray(0x58, 1, blob.Add(w.data(), w.size()));
        }

        // materials (two-sided hides any source winding mismatch)
        {
            std::vector<uint8_t> data;
            for (const Material& m : materials)
            {
                // blended FX surfaces: unlit and no depth write, so stacked
                // transparent sheets stay visible through each other
                AppendU16(data, m.blend ? 0x15 : 0x04);
                AppendU16(data, kBlendMap[m.blend < 8 ? m.blend : 0]);
            }
            blob.SetArray(0x70, uint32_t(materials.size()),
                          blob.Add(data.data(), data.size()));
        }

        // bone combos = source bone lookup verbatim (regions slice into it)
        if (!model.lookup.empty())
            blob.SetArray(0x78, uint32_t(model.lookup.size()),
                          blob.Add(model.lookup.data(), model.lookup.size() * 2));
        else
        {
            const uint16_t zero = 0;
            blob.SetArray(0x78, 1, blob.Add(&zero, 2));
        }
        {
            std::vector<uint8_t> combos;
            for (uint16_t i = 0; i < uint16_t(materials.size()); ++i) AppendU16(combos, i);
            blob.SetArray(0x80, uint32_t(materials.size()),
                          blob.Add(combos.data(), combos.size()));
            const uint16_t uv0 = 0, w0 = 0;
            blob.SetArray(0x88, 1, blob.Add(&uv0, 2));
            blob.SetArray(0x90, 1, blob.Add(&w0, 2));
        }

        // texture transforms: one linear uv-translation per scrolling material;
        // the combo table maps material index -> transform (-1 = static)
        {
            std::vector<int16_t> combos(materials.size(), -1);
            std::vector<size_t> scrollers;
            for (size_t i = 0; i < materials.size(); ++i)
                if (materials[i].uvRate[0] != 0 || materials[i].uvRate[1] != 0)
                {
                    combos[i] = int16_t(scrollers.size());
                    scrollers.push_back(i);
                }
            if (!scrollers.empty())
            {
                // each scroller rides a global sequence: wall-clock time, so the
                // scroll speed never scales with animation playback speed. the
                // outer M2Array is still one inner slot PER MODEL SEQUENCE (like
                // every other track in this file) because the engine indexes it
                // by the currently active sequence regardless of global_sequence;
                // a single-entry outer array only animated sequence 0
                const uint32_t nseq = uint32_t(bake.seqs.empty() ? 1 : bake.seqs.size());
                std::vector<uint8_t> gseqs;
                std::vector<uint8_t> xf;
                for (size_t s : scrollers)
                {
                    const Material& m = materials[s];
                    // uvRate is texture cycles per millisecond (resolved against
                    // the source track's ms duration)
                    const float fastest = std::max(std::fabs(m.uvRate[0]),
                                                   std::fabs(m.uvRate[1]));
                    const uint32_t period =
                        uint32_t(std::max(1.0f, std::round(1.0f / fastest)));
                    // whole texture cycles over the period: the loop never lands
                    // mid-cycle, so the wrap seam stays invisible
                    float d[2];
                    for (int a = 0; a < 2; ++a)
                    {
                        d[a] = m.uvRate[a] * period;
                        d[a] = (d[a] < 0 ? -1.0f : 1.0f) * std::round(std::fabs(d[a]));
                    }
                    const uint32_t ts[2] = { 0, period };
                    const float va[6] = { 0, 0, 0, d[0], d[1], 0 };
                    const uint32_t tOff = blob.Add(ts, sizeof(ts));
                    const uint32_t vOff = blob.Add(va, sizeof(va));
                    std::vector<uint8_t> outerTs, outerVa;
                    for (uint32_t i = 0; i < nseq; ++i) { AppendU32(outerTs, 2); AppendU32(outerTs, tOff); }
                    for (uint32_t i = 0; i < nseq; ++i) { AppendU32(outerVa, 2); AppendU32(outerVa, vOff); }
                    const uint32_t tsTab = blob.Add(outerTs.data(), outerTs.size());
                    const uint32_t vaTab = blob.Add(outerVa.data(), outerVa.size());
                    const int16_t g = int16_t(gseqs.size() / 4);
                    AppendU32(gseqs, period);
                    AppendTrack(xf, { 1, g, nseq, tsTab, nseq, vaTab }); // translation
                    AppendTrack(xf, { 0, -1, 0, 0, 0, 0 });              // rotation
                    AppendTrack(xf, { 0, -1, 0, 0, 0, 0 });              // scaling
                }
                blob.SetArray(0x14, uint32_t(scrollers.size()),
                              blob.Add(gseqs.data(), gseqs.size()));
                blob.SetArray(0x60, uint32_t(scrollers.size()),
                              blob.Add(xf.data(), xf.size()));
            }
            blob.SetArray(0x98, uint32_t(combos.size()),
                          blob.Add(combos.data(), combos.size() * 2));
        }

        // particle emitters: static parameters, three-phase FBlocks, flipbook cycling
        if (!model.particles.empty())
        {
            std::vector<uint8_t> all;
            for (size_t i = 0; i < model.particles.size(); ++i)
            {
                const Emitter& p = model.particles[i];
                std::vector<uint8_t> rec;
                AppendU32(rec, uint32_t(i));
                AppendU32(rec, 0x20000); // HEAD quads: neither head nor tail = zero-vert draw division
                // position is model-space: the bone matrix transforms model-space
                // points, so the emitter sits at its bone's rest pivot
                const bool hasPivot = p.bone < bake.pivots.size();
                for (int a = 0; a < 3; ++a)
                    AppendF(rec, hasPivot ? bake.pivots[p.bone][a] : 0.0f);
                AppendU16(rec, uint16_t(p.bone));
                AppendU16(rec, parTex[i]);
                for (int k = 0; k < 4; ++k) AppendU32(rec, 0); // geometry / recursion names
                rec.push_back(3);  // blend add
                rec.push_back(1);  // plane emitter: directional +Z jet the bone orients
                AppendU16(rec, 0); // particleColorIndex
                rec.push_back(0);  // particleType
                rec.push_back(0);  // headOrTail
                AppendU16(rec, 0); // textureTileRotation
                AppendU16(rec, std::max<uint16_t>(1, p.rows));
                AppendU16(rec, std::max<uint16_t>(1, p.cols));

                AppendTrack(rec, StaticFloatTrack(blob, (p.speed[0] + p.speed[1]) / 2, nseq));
                AppendTrack(rec, StaticFloatTrack(blob, 1.0f, nseq));
                // cone: vertical = polar half-angle, horizontal = full revolution
                AppendTrack(rec, StaticFloatTrack(blob, p.spread[0], nseq));
                AppendTrack(rec, StaticFloatTrack(blob, 3.14159265f, nseq));
                AppendTrack(rec, StaticFloatTrack(blob, -p.zacc, nseq));
                AppendTrack(rec, StaticFloatTrack(blob, (p.life[0] + p.life[1]) / 2, nseq));
                AppendF(rec, (p.life[1] - p.life[0]) / 2);
                AppendTrack(rec, BakedOrStaticFloatTrack(blob, bake, i, p.rate, nseq));
                AppendF(rec, 0);
                AppendTrack(rec, StaticFloatTrack(blob, p.areaSize[0], nseq));
                AppendTrack(rec, StaticFloatTrack(blob, p.areaSize[1], nseq));
                AppendTrack(rec, StaticFloatTrack(blob, 0.0f, nseq));

                {
                    std::vector<uint8_t> cv, av, sv;
                    for (int c = 0; c < 3; ++c)
                    {
                        // source colors are BGRA bytes, the client field is RGB floats
                        for (int a = 0; a < 3; ++a)
                        {
                            float ch = p.colors[c][2 - a];
                            if (tint) ch = ch * tint[a] / 255.0f;
                            AppendF(cv, ch);
                        }
                        AppendU16(av, uint16_t(p.colors[c][3] * 32767 / 255));
                        AppendF(sv, p.sizes[c]); AppendF(sv, p.sizes[c]);
                    }
                    AppendFBlock(rec, blob, ThreePhase(p.mid[1]), cv, 3);
                    AppendFBlock(rec, blob, ThreePhase(p.mid[2]), av, 3);
                    AppendFBlock(rec, blob, ThreePhase(p.mid[0]), sv, 3);
                }
                AppendF(rec, 0); AppendF(rec, 0); // scaleVary

                {
                    const int ncells = int(p.cellLast) - int(p.cellFirst) + 1;
                    std::vector<uint16_t> times;
                    std::vector<uint8_t> cells;
                    if (ncells < 2)
                    {
                        times = { 0, 32767 };
                        AppendU16(cells, p.cellFirst); AppendU16(cells, p.cellFirst);
                    }
                    else
                    {
                        for (int k = 0; k < ncells; ++k)
                        {
                            times.push_back(uint16_t(k * 32767 / (ncells - 1)));
                            AppendU16(cells, uint16_t(p.cellFirst + k));
                        }
                    }
                    AppendFBlock(rec, blob, times, cells, uint32_t(times.size()));
                    std::vector<uint8_t> tailCells;
                    AppendU16(tailCells, p.cellFirst); AppendU16(tailCells, p.cellFirst);
                    AppendFBlock(rec, blob, { 0, 32767 }, tailCells, 2);
                }

                AppendF(rec, 0); AppendF(rec, 0);                            // tailLen, twinkleSpeed
                AppendF(rec, 1);  // twinklePercent: visible fraction, below 1 culls particles
                AppendF(rec, 1); AppendF(rec, 1);                            // twinkle scale
                AppendF(rec, 1); AppendF(rec, 0);                            // burst, drag
                for (int k = 0; k < 4; ++k) AppendF(rec, 0);                 // spin block
                for (int k = 0; k < 10; ++k) AppendF(rec, 0);                // tumble, wind, windTime
                for (int k = 0; k < 4; ++k) AppendF(rec, 0);                 // follow params
                AppendU32(rec, 0); AppendU32(rec, 0);                        // splinePoints
                // enabledIn left empty: a missing track keeps the emitter always on
                AppendTrack(rec, { 0, -1, 0, 0, 0, 0 });
                all.insert(all.end(), rec.begin(), rec.end());
            }
            blob.SetArray(0x128, uint32_t(model.particles.size()),
                          blob.Add(all.data(), all.size()));
        }

        // ribbon emitters: static parameters; the trail itself comes from bone
        // motion plus the width / lifetime pair and the trail texture
        if (!model.ribbons.empty())
        {
            std::vector<uint8_t> all;
            for (size_t i = 0; i < model.ribbons.size(); ++i)
            {
                const Ribbon& r = model.ribbons[i];
                std::vector<uint8_t> rec;
                AppendU32(rec, uint32_t(-1));
                AppendU32(rec, r.bone);
                const bool hasPivot = r.bone < bake.pivots.size();
                for (int a = 0; a < 3; ++a)
                    AppendF(rec, hasPivot ? bake.pivots[r.bone][a] : 0.0f);
                const uint16_t ti = ribTex[i];
                const uint16_t mi = uint16_t(r.mat < materials.size() ? r.mat : 0);
                AppendU32(rec, 1); AppendU32(rec, blob.Add(&ti, 2));
                AppendU32(rec, 1); AppendU32(rec, blob.Add(&mi, 2));
                float col[3];
                for (int a = 0; a < 3; ++a) col[a] = tint ? tint[a] / 255.0f : 1.0f;
                AppendTrack(rec, StaticTrack(blob, col, 12, nseq));
                const uint16_t opaque = 0x7FFF;
                AppendTrack(rec, StaticTrack(blob, &opaque, 2, nseq));
                // symmetric extents around the bone; above and below must differ
                AppendTrack(rec, StaticFloatTrack(blob, r.width * 0.5f, nseq));
                AppendTrack(rec, StaticFloatTrack(blob, r.width * 0.495f, nseq));
                AppendF(rec, 30.0f);                            // edgesPerSecond
                const float life = (r.length > 0) ? r.length : 1.0f;
                AppendF(rec, life < 1.0f ? life : 1.0f);        // edgeLifetime
                AppendF(rec, 0.0f);                             // gravity
                AppendU16(rec, 1); AppendU16(rec, 1);           // texture rows, cols
                const uint16_t slot0 = 0;
                AppendTrack(rec, StaticTrack(blob, &slot0, 2, nseq));
                const uint8_t on = 1;
                AppendTrack(rec, StaticTrack(blob, &on, 1, nseq));
                AppendU16(rec, 0);                              // priorityPlane
                rec.push_back(uint8_t(0xFF));                   // no color index
                rec.push_back(uint8_t(0xFF));                   // no texture transform
                all.insert(all.end(), rec.begin(), rec.end());
            }
            blob.SetArray(0x120, uint32_t(model.ribbons.size()),
                          blob.Add(all.data(), all.size()));
        }

        // bounds + collision bounds
        for (int a = 0; a < 3; ++a) blob.Put<float>(0xA0 + a * 4, model.bmin[a]);
        for (int a = 0; a < 3; ++a) blob.Put<float>(0xAC + a * 4, model.bmax[a]);
        blob.Put<float>(0xB8, model.radius);
        for (int a = 0; a < 3; ++a) blob.Put<float>(0xBC + a * 4, model.bmin[a]);
        for (int a = 0; a < 3; ++a) blob.Put<float>(0xC8 + a * 4, model.bmax[a]);
        blob.Put<float>(0xD4, model.radius);

        blob.Put<uint32_t>(0x00, 0x3032444D); // MD20
        blob.Put<uint32_t>(0x04, 264);
        out = std::move(blob.b);
    }

    void BuildSkin(const Model& model, std::vector<uint8_t>& out)
    {
        Blob blob;
        blob.b.assign(kSkinHeader, 0);

        {
            std::vector<uint8_t> lk;
            for (uint32_t i = 0; i < model.verts.size(); ++i) AppendU16(lk, uint16_t(i));
            blob.SetArray(0x04, uint32_t(model.verts.size()), blob.Add(lk.data(), lk.size()));
        }

        // triangles: region-relative values rebased to global vertex ids in place
        {
            std::vector<uint16_t> tri = model.faces;
            for (const Region& r : model.regions)
                for (uint32_t k = r.firstFace; k < r.firstFace + r.numFaces && k < tri.size(); ++k)
                    tri[k] = uint16_t(r.firstVert + model.faces[k]);
            blob.SetArray(0x0C, uint32_t(tri.size()), blob.Add(tri.data(), tri.size() * 2));
        }

        // per-vertex bone properties: lookup-relative indices, zeroed on unweighted slots
        {
            std::vector<uint8_t> props;
            props.reserve(model.verts.size() * 4);
            for (const Vertex& v : model.verts)
                for (int k = 0; k < 4; ++k)
                    props.push_back(v.w[k] ? v.bl[k] : 0);
            blob.SetArray(0x14, uint32_t(model.verts.size()),
                          blob.Add(props.data(), props.size()));
        }

        uint16_t maxBones = 1;
        {
            std::vector<uint8_t> subs;
            for (const Region& r : model.regions)
            {
                Vec3 cx = { 0, 0, 0 };
                const uint32_t n = r.numVerts ? r.numVerts : 1;
                for (uint32_t i = r.firstVert; i < r.firstVert + r.numVerts; ++i)
                    for (int a = 0; a < 3; ++a) cx[a] += model.verts[i].pos[a];
                for (int a = 0; a < 3; ++a) cx[a] /= float(n);
                float srad = 0;
                uint16_t infl = 1;
                for (uint32_t i = r.firstVert; i < r.firstVert + r.numVerts; ++i)
                {
                    float dd = 0;
                    for (int a = 0; a < 3; ++a)
                    {
                        const float d = model.verts[i].pos[a] - cx[a];
                        dd += d * d;
                    }
                    srad = std::max(srad, dd);
                    uint16_t used = 0;
                    for (int k = 0; k < 4; ++k) if (model.verts[i].w[k]) ++used;
                    infl = std::max(infl, used);
                }
                srad = std::sqrt(srad);
                maxBones = std::max(maxBones, r.numBones);

                AppendU16(subs, 0); AppendU16(subs, 0);
                AppendU16(subs, uint16_t(r.firstVert)); AppendU16(subs, uint16_t(r.numVerts));
                AppendU16(subs, uint16_t(r.firstFace)); AppendU16(subs, uint16_t(r.numFaces));
                AppendU16(subs, r.numBones ? r.numBones : 1);
                AppendU16(subs, r.firstLookup);
                AppendU16(subs, infl); AppendU16(subs, 0);
                for (float f : cx) AppendF(subs, f);
                for (float f : cx) AppendF(subs, f);
                AppendF(subs, srad);
            }
            blob.SetArray(0x1C, uint32_t(model.regions.size()),
                          blob.Add(subs.data(), subs.size()));
        }

        {
            const std::vector<float> alphas = BatchAlphas(model);
            std::vector<uint8_t> bat;
            for (const Batch& b : model.batches)
            {
                uint16_t ci = 0xFFFF;
                const auto itA = std::find(alphas.begin(), alphas.end(), b.alpha);
                if (b.alpha < 1.0f && itA != alphas.end())
                    ci = uint16_t(itA - alphas.begin());
                bat.push_back(0); bat.push_back(0);          // flags, priorityPlane
                AppendU16(bat, 0);                            // shaderId
                AppendU16(bat, b.region); AppendU16(bat, b.region);
                AppendU16(bat, ci);                           // colorIndex
                AppendU16(bat, uint16_t(b.matIndex));
                AppendU16(bat, 0);                            // materialLayer
                AppendU16(bat, 1);                            // textureCount
                AppendU16(bat, uint16_t(b.matIndex));         // textureCombo = material index
                AppendU16(bat, 0); AppendU16(bat, 0);
                AppendU16(bat, uint16_t(b.matIndex));         // textureTransformCombo
            }
            blob.SetArray(0x24, uint32_t(model.batches.size()),
                          blob.Add(bat.data(), bat.size()));
        }

        blob.Put<uint32_t>(0x00, 0x4E494B53); // 'SKIN'
        blob.Put<uint32_t>(0x2C, std::max<uint32_t>(21, maxBones));
        out = std::move(blob.b);
    }
}
