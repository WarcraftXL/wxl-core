// Host face for the m3 format: serves drop-in source M3 models as client models.
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

#include "config.hpp"
#include "Host.hpp"
#include "common/Log.hpp"

#include "engine/assets/shared/models/m3/M2Build.hpp"
#include "engine/assets/shared/models/m3/Md34.hpp"
#include "engine/assets/shared/models/m3/Options.hpp"
#include "engine/assets/shared/textures/dds/Dds.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Drop-in flow: put "<base>.m3" inside a loose Patch*.MPQ folder at the served path --
// a request for "<base>.m2" converts it on the spot (model + skin cached), the optional
// "<base>.m3a" library and "<base>.m3cfg" options sit beside it, and any requested .blp
// transcodes from the .dds of the same name in the same folder.
namespace
{
    namespace m3   = wxl::modern::assets::m3;
    namespace dds_ = wxl::modern::assets::textures::dds; // "dds" is also a local var name below

    struct State
    {
        std::mutex mutex;
        bool scanned = false;
        std::vector<std::string> looseRoots; // absolute Patch*.MPQ folders
        std::vector<m3::ModelSource> sources;
        // served name -> source index (model and skin names)
        std::unordered_map<std::string, size_t> byName;
        // served name -> converted bytes
        std::unordered_map<std::string, std::vector<uint8_t>> cache;
        // names known to have no drop-in source
        std::unordered_map<std::string, bool> miss;
        // served texture names used as an additive particle/ribbon atlas: routed through
        // luminance-derived alpha at serve time (see DdsToBlp)
        std::unordered_map<std::string, bool> fxTextures;
    };

    State& S()
    {
        static State s;
        return s;
    }

    // Loose-root presence, readable without the state mutex: -1 unscanned, 0 none, 1 present. The
    // provider runs for EVERY file the client opens, so the no-drop-ins case must not take a lock.
    std::atomic<int> g_looseState{ -1 };

    /** @brief Cheap pre-filter: only model/skin/texture names can ever be m3 drop-ins. */
    bool MightServe(std::string_view name)
    {
        auto endsWith = [&](const char* suffix) {
            const size_t n = std::strlen(suffix);
            if (n > name.size()) return false;
            for (size_t i = 0; i < n; ++i)
                if (std::tolower(static_cast<unsigned char>(name[name.size() - n + i])) != suffix[i])
                    return false;
            return true;
        };
        return endsWith(".m2") || endsWith(".skin") || endsWith(".blp");
    }

    bool ReadWhole(const std::string& path, std::vector<uint8_t>& out)
    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return false;
        const std::streamsize n = f.tellg();
        f.seekg(0);
        out.resize(size_t(n));
        return bool(f.read(reinterpret_cast<char*>(out.data()), n));
    }

    std::string SkinName(const std::string& target)
    {
        // "<base>.m2" -> "<base>00.skin"
        return target.substr(0, target.size() - 3) + "00.skin";
    }

    // Loose Patch*.MPQ FOLDERS under Data are the drop-in search roots.
    void ScanLooseRootsOnce()
    {
        State& s = S();
        if (s.scanned) return;
        s.scanned = true;

        const std::string root = wxl::host::ClientRoot();
        if (root.empty()) return;
        std::error_code ec;
        for (const auto& it : std::filesystem::directory_iterator(root + "\\Data", ec))
        {
            if (!it.is_directory()) continue;
            std::string low = it.path().filename().string();
            std::transform(low.begin(), low.end(), low.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            if (low.rfind("patch", 0) == 0 && low.size() > 4 &&
                low.compare(low.size() - 4, 4, ".mpq") == 0)
                s.looseRoots.push_back(it.path().string());
        }
        g_looseState.store(s.looseRoots.empty() ? 0 : 1, std::memory_order_release);
        WLOG_INFO("modern-m3: %u loose roots", unsigned(s.looseRoots.size()));
    }

    /**
     * @brief Resolves a served model name to the drop-in source beside it, registering
     *        the model and skin names on success.
     * @param key  normalized served name ending in ".m2"
     * @return source index, or SIZE_MAX when no drop-in source exists
     */
    size_t ResolveDropin(const std::string& key)
    {
        State& s = S();
        const auto known = s.byName.find(key);
        if (known != s.byName.end()) return known->second;
        if (s.miss.count(key)) return SIZE_MAX;

        const std::string stem = key.substr(0, key.size() - 3); // strip ".m2"
        for (const std::string& root : s.looseRoots)
        {
            const std::string m3Path = root + "\\" + stem + ".m3";
            if (!std::filesystem::exists(m3Path)) continue;

            m3::ModelSource src;
            src.target = key;
            src.m3 = m3Path;
            const std::string m3a = root + "\\" + stem + ".m3a";
            if (std::filesystem::exists(m3a)) src.m3a = m3a;

            const size_t sep = key.rfind('\\');
            const std::string dir = (sep != std::string::npos) ? key.substr(0, sep + 1) : "";
            src.tex = dir;
            src.texroot = root + (dir.empty() ? "" : "\\" + dir.substr(0, dir.size() - 1));

            std::vector<uint8_t> cfg;
            if (ReadWhole(root + "\\" + stem + ".m3cfg", cfg))
                m3::ParseOptions(std::string(cfg.begin(), cfg.end()), src);

            s.sources.push_back(std::move(src));
            const size_t idx = s.sources.size() - 1;
            s.byName.emplace(key, idx);
            s.byName.emplace(SkinName(key), idx);
            WLOG_INFO("modern-m3: drop-in %s <- %s", key.c_str(), m3Path.c_str());
            return idx;
        }
        s.miss.emplace(key, true);
        return SIZE_MAX;
    }

    /** @brief Maps any served name (model or skin) to its source index, or SIZE_MAX. */
    size_t SourceFor(const std::string& key)
    {
        State& s = S();
        const auto it = s.byName.find(key);
        if (it != s.byName.end()) return it->second;
        if (key.size() > 3 && key.compare(key.size() - 3, 3, ".m2") == 0)
            return ResolveDropin(key);
        if (key.size() > 7 && key.compare(key.size() - 7, 7, "00.skin") == 0)
        {
            const size_t idx = ResolveDropin(key.substr(0, key.size() - 7) + ".m2");
            return (idx != SIZE_MAX && s.byName.count(key)) ? idx : SIZE_MAX;
        }
        return SIZE_MAX;
    }

    // Runs the full conversion for one source and caches the model + skin images.
    bool Convert(const m3::ModelSource& e)
    {
        std::vector<uint8_t> m3bytes;
        if (!ReadWhole(e.m3, m3bytes))
        {
            WLOG_INFO("modern-m3: cannot read %s", e.m3.c_str());
            return false;
        }

        const m3::Mat4 frame = m3::ModelFrame(e.lift);
        m3::Model model;
        if (!m3::ParseModel(m3bytes.data(), m3bytes.size(), frame, model))
        {
            WLOG_INFO("modern-m3: parse failed %s", e.m3.c_str());
            return false;
        }
        if (e.ribmesh) m3::RibbonsToMesh(model, e.ribtilt, e.riblen, e.ribbasewidth);

        m3::BakeResult bake;
        bool baked = false;
        if (!e.m3a.empty())
        {
            std::vector<uint8_t> libBytes;
            m3::AnimLib lib;
            if (ReadWhole(e.m3a, libBytes) && m3::ParseAnimLib(std::move(libBytes), lib))
                baked = m3::BakeSequences(model, lib, m3::ParseSeqMap(e.map), frame, bake);
            else
                WLOG_INFO("modern-m3: anim library unreadable %s", e.m3a.c_str());
        }
        if (!baked) m3::StaticBake(model, bake);

        // the model's own animation tables: ambient bone loops and layer uv scrolls
        {
            m3::AnimLib self;
            std::vector<uint8_t> selfBytes = m3bytes;
            if (m3::ParseAnimLib(std::move(selfBytes), self))
            {
                for (size_t mi = 0; mi < model.materials.size(); ++mi)
                {
                    auto& m = model.materials[mi];
                    if (!m.uvAnim) continue;
                    for (uint32_t si = 0; si < self.stcs.size(); ++si)
                    {
                        m3::LibTrack t;
                        if (!self.Track(si, m.uvAnim, 1, 8, t) || t.count < 2) continue;
                        const float dur = float(t.times[t.count - 1] - t.times[0]);
                        if (dur <= 0) continue;
                        float v0[2], vN[2];
                        std::memcpy(v0, t.values, 8);
                        std::memcpy(vN, t.values + (t.count - 1) * 8, 8);
                        m.uvRate[0] = (vN[0] - v0[0]) / dur;
                        m.uvRate[1] = (vN[1] - v0[1]) / dur;
                        // sheet meshes run U along the length while the source
                        // ribbon scrolls its along-axis on V: flow follows the wing,
                        // negated so it runs base -> tip
                        if (m.sheetUv)
                        {
                            std::swap(m.uvRate[0], m.uvRate[1]);
                            m.uvRate[0] = -m.uvRate[0];
                        }
                        WLOG_INFO("modern-m3: uv scroll mat%u %.4f,%.4f /s",
                                               unsigned(mi), m.uvRate[0] * 1000.0f,
                                               m.uvRate[1] * 1000.0f);
                        break;
                    }
                }
                if (!e.ambient.empty() &&
                    m3::OverlayAmbient(model, self, e.ambient, frame, bake))
                    WLOG_INFO("modern-m3: ambient '%s' overlaid",
                                           e.ambient.c_str());
            }
        }

        std::string internal = e.target;
        const size_t sep = internal.rfind('\\');
        if (sep != std::string::npos) internal = internal.substr(sep + 1);
        internal = internal.substr(0, internal.size() - 3);

        State& s = S();
        auto& m2 = s.cache[e.target];
        auto& skin = s.cache[SkinName(e.target)];
        std::vector<std::string> fxTextures;
        m3::BuildM2(model, internal, e.tex, bake, e.parcolor, m2, &fxTextures);
        m3::BuildSkin(model, skin);
        for (const std::string& tex : fxTextures) s.fxTextures[tex] = true;
        WLOG_INFO("modern-m3: %s verts=%u bones=%u seqs=%u emitters=%u ribbons=%u",
                               e.target.c_str(), unsigned(model.verts.size()),
                               unsigned(model.bones.size()), unsigned(bake.seqs.size()),
                               unsigned(model.particles.size()),
                               unsigned(model.ribbons.size()));
        return true;
    }

    /**
     * @brief Resolves a served texture name to a .dds sibling in a loose root.
     * @param name  normalized served name
     * @param dds   receives the source path
     * @return true when a matching .dds exists
     */
    bool TextureSource(const std::string& name, std::string& dds)
    {
        if (name.size() < 5 || name.compare(name.size() - 4, 4, ".blp") != 0) return false;
        const std::string rel = name.substr(0, name.size() - 4) + ".dds";
        for (const std::string& root : S().looseRoots)
        {
            const std::string candidate = root + "\\" + rel;
            if (std::filesystem::exists(candidate))
            {
                dds = candidate;
                return true;
            }
        }
        return false;
    }

    bool Provide(std::string_view name, std::vector<uint8_t>& out)
    {
        if (!MightServe(name)) return false;
        if (g_looseState.load(std::memory_order_acquire) == 0) return false;

        State& s = S();
        std::lock_guard<std::mutex> lock(s.mutex);
        ScanLooseRootsOnce();
        if (s.looseRoots.empty()) return false;

        const std::string key = m3::NormalizePath(std::string(name));
        if (s.miss.count(key)) return false;
        const auto cached = s.cache.find(key);
        if (cached != s.cache.end())
        {
            out = cached->second;
            return true;
        }

        const size_t idx = SourceFor(key);
        if (idx != SIZE_MAX)
        {
            if (!Convert(s.sources[idx])) return false;
            const auto hit = s.cache.find(key);
            if (hit == s.cache.end()) return false;
            out = hit->second;
            return true;
        }

        std::string dds;
        if (TextureSource(key, dds))
        {
            std::vector<uint8_t> raw;
            const bool luminanceAlpha = s.fxTextures.count(key) != 0;
            if (ReadWhole(dds, raw) && dds_::DdsToBlp(raw.data(), raw.size(), out, luminanceAlpha))
            {
                s.cache.emplace(key, out);
                WLOG_INFO("modern-m3: texture %s <- %s", key.c_str(), dds.c_str());
                return true;
            }
        }
        // Drop-ins are static for the session: remember the miss so the per-open cost of this name
        // collapses to one hash lookup instead of repeated filesystem probes.
        s.miss.emplace(key, true);
        return false;
    }

    bool Exists(std::string_view name)
    {
        if (!MightServe(name)) return false;
        if (g_looseState.load(std::memory_order_acquire) == 0) return false;

        State& s = S();
        std::lock_guard<std::mutex> lock(s.mutex);
        ScanLooseRootsOnce();
        if (s.looseRoots.empty()) return false;

        const std::string key = m3::NormalizePath(std::string(name));
        if (s.miss.count(key)) return false;
        if (s.cache.count(key) || SourceFor(key) != SIZE_MAX) return true;
        std::string dds;
        if (TextureSource(key, dds)) return true;
        s.miss.emplace(key, true);
        return false;
    }

    struct Registrar
    {
        Registrar()
        {
            // Install the host M3 provider/exists hooks only when the modern-M3 format is compiled in; disabled, the registrar is inert.
            if constexpr (wxl::features::kModernM3)
            {
                wxl::host::RegisterProvider("modern-m3", &Provide);
                wxl::host::RegisterExists("modern-m3", &Exists);
            }
        }
    };
    Registrar g_registrar;
}
