// Applies host-side compatibility fixes for retail equipment models before client parsing.
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

#include "EquipmentFixes.hpp"

#include "Host.hpp"
#include "common/Log.hpp"
#include "mpq/MpqStore.hpp"
#include "engine/assets/m2/M2Format.hpp"

#include "engine/assets/shared/common/Env.hpp"
#include "engine/assets/shared/common/Text.hpp"
#include "engine/assets/shared/models/m2/Textures.hpp"

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>

namespace wxl::modern::assets::m2::equipment
{
    namespace fmt  = wxl::structure::m2;
    namespace tex  = wxl::modern::assets::m2::textures;
    namespace text = wxl::modern::assets::common::text;
    namespace env  = wxl::modern::assets::common::env;

    namespace
    {
#pragma pack(push, 1)
        // A synthesized single-key translation track, appended past the model's original bytes and
        // pointed to by one bone's M2CompBone::translation. The outer M2Array in translation.timestamps
        // / translation.values always holds exactly one M2Array<T> descriptor (the pre-Legion per-sequence
        // indirection this client's loader expects); that inner descriptor in turn holds the one actual
        // keyframe this synth needs.
        struct AppendedTranslationTrack
        {
            fmt::M2Array innerTimestamps; // 0x00 -> timestampValue
            uint32_t     timestampValue;  // 0x08  (left at 0: the single key sits at t=0)
            fmt::M2Array innerValues;     // 0x0C -> value
            float        value[3];        // 0x14
        };
#pragma pack(pop)
        static_assert(sizeof(AppendedTranslationTrack) == 0x20, "AppendedTranslationTrack");

        // Every playable race's 2-letter model-name code (e.g. "dr" = draenei); shared by HelmRaceId
        // (which race a helm's "<code><m|f>" filename suffix names) and StripRaceGenderSuffix (which
        // trailing "_<code><m|f>" a model rule key strips).
        constexpr std::string_view kRaceCodes[] = {
            "be", "dr", "dw", "gn", "hu", "ni", "or", "sc", "ta", "tr", "sk", "go"
        };

        bool IsRaceCode(std::string_view race)
        {
            for (const auto r : kRaceCodes) if (race == r) return true;
            return false;
        }

        bool HelmRaceId(std::string_view name, std::string& id)
        {
            const std::string s = text::LowerSlashed(name);
            const size_t slash = s.find_last_of('\\');
            const size_t start = slash == std::string::npos ? 0 : slash + 1;
            const std::string_view base(s.data() + start, s.size() - start);
            size_t ext = base.rfind('.');
            if (ext == std::string_view::npos) ext = base.size();
            if (ext < 3 || (base.substr(ext) != ".m2" && base.substr(ext) != ".mdx")) return false;
            if (base.rfind("helm_", 0) != 0 && base.rfind("helmet_", 0) != 0 &&
                base.find("_helm_") == std::string_view::npos) return false;

            if (ext >= 4 && base[ext - 2] == '_')
            {
                id.assign(base.substr(ext - 4, 2));
                id.push_back(base[ext - 1]);
            }
            else id.assign(base.substr(ext - 3, 3));

            if (id.size() != 3 || (id[2] != 'm' && id[2] != 'f')) return false;
            return IsRaceCode(std::string_view(id).substr(0, 2));
        }

        struct HelmOffsetRule
        {
            std::string model;
            std::string raceSex;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            bool hasX = false, hasY = false, hasZ = false;
            bool add = true, disable = false;
        };

        std::once_flag g_helmOffsetsOnce;
        std::vector<HelmOffsetRule> g_helmOffsetRules;

        std::string TrimCopy(std::string value)
        {
            size_t first = 0;
            while (first < value.size() && (value[first] == ' ' || value[first] == '\t')) ++first;
            size_t last = value.size();
            while (last > first)
            {
                const char c = value[last - 1];
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
                --last;
            }
            return value.substr(first, last - first);
        }

        std::string NormalizeCsvName(const std::string& value)
        {
            std::string out;
            for (char c : value)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (std::isalnum(static_cast<unsigned char>(c))) out.push_back(c);
            }
            return out;
        }

        std::vector<std::string> ParseCsvLine(const char* line)
        {
            std::vector<std::string> fields;
            std::string field;
            bool quoted = false;
            for (const char* p = line; *p; ++p)
            {
                const char c = *p;
                if ((c == '\r' || c == '\n') && !quoted) break;
                if (quoted)
                {
                    if (c == '"')
                    {
                        if (p[1] == '"') { field.push_back('"'); ++p; }
                        else quoted = false;
                    }
                    else field.push_back(c);
                }
                else if (c == '"') quoted = true;
                else if (c == ',') { fields.push_back(TrimCopy(field)); field.clear(); }
                else field.push_back(c);
            }
            fields.push_back(TrimCopy(field));
            return fields;
        }

        int FindCsvColumn(const std::vector<std::string>& header, const char* name)
        {
            const std::string wanted = NormalizeCsvName(name);
            for (size_t i = 0; i < header.size(); ++i)
                if (NormalizeCsvName(header[i]) == wanted) return static_cast<int>(i);
            return -1;
        }

        const char* CsvField(const std::vector<std::string>& row, int column)
        {
            return column >= 0 && static_cast<size_t>(column) < row.size() ? row[column].c_str() : "";
        }

        bool ParseFloat(const char* text, float& out)
        {
            if (!text || !*text) return false;
            char* end = nullptr;
            const float value = std::strtof(text, &end);
            if (end == text) return false;
            out = value;
            return true;
        }

        std::string NormalizeRaceSex(std::string value)
        {
            value = text::LowerSlashed(TrimCopy(value));
            std::string out;
            for (char c : value)
                if (std::isalnum(static_cast<unsigned char>(c))) out.push_back(c);
            return out;
        }

        std::string StripModelExtension(std::string value)
        {
            const size_t dot = value.rfind('.');
            if (dot != std::string::npos && (value.substr(dot) == ".m2" || value.substr(dot) == ".mdx"))
                value.resize(dot);
            return value;
        }

        std::string StripRaceGenderSuffix(std::string stem)
        {
            if (stem.size() >= 5)
            {
                const size_t n = stem.size();
                if (stem[n - 5] == '_' && stem[n - 2] == '_' && (stem[n - 1] == 'm' || stem[n - 1] == 'f') &&
                    IsRaceCode(std::string_view(stem.data() + n - 4, 2)))
                { stem.resize(n - 5); return stem; }
            }
            if (stem.size() >= 4)
            {
                const size_t n = stem.size();
                if (stem[n - 4] == '_' && (stem[n - 1] == 'm' || stem[n - 1] == 'f') &&
                    IsRaceCode(std::string_view(stem.data() + n - 3, 2))) stem.resize(n - 4);
            }
            return stem;
        }

        std::string FileNamePart(const std::string& value)
        {
            const size_t slash = value.find_last_of('\\');
            return slash == std::string::npos ? value : value.substr(slash + 1);
        }

        std::string NormalizeModelRuleKey(std::string_view value)
        {
            return StripModelExtension(text::LowerSlashed(TrimCopy(std::string(value))));
        }

        uint32_t LoadHelmOffsetText(const char* source, const std::string& text)
        {
            std::istringstream stream(text);
            std::string line;
            if (!std::getline(stream, line)) return 0;
            const auto header = ParseCsvLine(line.c_str());
            int cModel = FindCsvColumn(header, "Model");
            if (cModel < 0) cModel = FindCsvColumn(header, "ModelStem");
            if (cModel < 0) cModel = FindCsvColumn(header, "Path");
            const int cRaceSex = FindCsvColumn(header, "RaceSex");
            const int cMode = FindCsvColumn(header, "Mode");
            int cX = FindCsvColumn(header, "X"), cY = FindCsvColumn(header, "Y"), cZ = FindCsvColumn(header, "Z");
            if (cX < 0) cX = FindCsvColumn(header, "OffsetX");
            if (cY < 0) cY = FindCsvColumn(header, "OffsetY");
            if (cZ < 0) cZ = FindCsvColumn(header, "OffsetZ");
            if (cModel < 0) { WLOG_WARN("modern-assets: helm offsets '%s' missing Model column", source); return 0; }

            uint32_t loaded = 0;
            while (std::getline(stream, line))
            {
                const auto row = ParseCsvLine(line.c_str());
                HelmOffsetRule rule;
                rule.model = NormalizeModelRuleKey(CsvField(row, cModel));
                if (rule.model.empty()) continue;
                rule.raceSex = NormalizeRaceSex(CsvField(row, cRaceSex));
                if (rule.raceSex == "all") rule.raceSex = "*";
                const std::string mode = NormalizeRaceSex(CsvField(row, cMode));
                rule.disable = mode == "disable" || mode == "disabled" || mode == "none" || mode == "skip";
                rule.add = !(mode == "set" || mode == "absolute" || mode == "override");
                rule.hasX = ParseFloat(CsvField(row, cX), rule.x);
                rule.hasY = ParseFloat(CsvField(row, cY), rule.y);
                rule.hasZ = ParseFloat(CsvField(row, cZ), rule.z);
                if (!rule.disable && !rule.hasX && !rule.hasY && !rule.hasZ) continue;
                g_helmOffsetRules.push_back(std::move(rule));
                ++loaded;
            }
            if (loaded) WLOG_INFO("modern-assets: loaded helm offsets '%s' rows=%u", source, loaded);
            return loaded;
        }

        void LoadHelmOffsetFile(const std::filesystem::path& path)
        {
            FILE* file = std::fopen(path.string().c_str(), "rb");
            if (!file) return;
            std::string text;
            char buffer[4096];
            for (;;)
            {
                const size_t n = std::fread(buffer, 1, sizeof(buffer), file);
                if (n) text.append(buffer, n);
                if (n < sizeof(buffer)) break;
            }
            std::fclose(file);
            if (!text.empty()) LoadHelmOffsetText(path.string().c_str(), text);
        }

        void LoadHelmOffsets()
        {
            std::call_once(g_helmOffsetsOnce, [] {
                const std::filesystem::path root(wxl::host::ClientRoot());
                if (!root.empty())
                {
                    LoadHelmOffsetFile(root / "WXLHelmOffsets.csv");
                    LoadHelmOffsetFile(root / "DBFilesClient" / "WXLHelmOffsets.csv");

                    wxl::host::mpq::MpqStore store;
                    if (store.Mount(root.string()))
                    {
                        std::vector<uint8_t> bytes;
                        if (store.ReadAll("DBFilesClient\\WXLHelmOffsets.csv", bytes) && !bytes.empty())
                            LoadHelmOffsetText("archives:DBFilesClient\\WXLHelmOffsets.csv",
                                std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
                    }

                    std::error_code ec;
                    for (const auto& entry : std::filesystem::directory_iterator(root / "Data", ec))
                    {
                        if (ec) break;
                        if (!entry.is_directory(ec)) continue;
                        const std::string folder = text::LowerSlashed(entry.path().filename().string());
                        if (!text::StartsWithCI(folder, "patch-") || folder.find(".mpq") == std::string::npos) continue;
                        LoadHelmOffsetFile(entry.path() / "DBFilesClient" / "WXLHelmOffsets.csv");
                    }
                }
                if (!g_helmOffsetRules.empty())
                    WLOG_INFO("modern-assets: helm offset sidecar ready rules=%zu", g_helmOffsetRules.size());
            });
        }

        int HelmRuleModelScore(const HelmOffsetRule& rule, const std::string& pathStem,
                               const std::string& pathBase, const std::string& fileStem,
                               const std::string& fileBase, const std::string& raceSex)
        {
            if (!rule.raceSex.empty() && rule.raceSex != "*" && rule.raceSex != raceSex) return -1;
            int score = -1;
            if (rule.model.find('\\') != std::string::npos)
            {
                if (rule.model == pathStem) score = 40;
                else if (rule.model == pathBase) score = 30;
            }
            else
            {
                if (rule.model == fileStem) score = 20;
                else if (rule.model == fileBase) score = 10;
            }
            if (score >= 0 && !rule.raceSex.empty() && rule.raceSex != "*") score += 5;
            return score;
        }

        bool ApplyHelmOffsetRule(std::string_view name, const std::string& id, float& x, float& y, float& z)
        {
            LoadHelmOffsets();
            const std::string pathStem = NormalizeModelRuleKey(name);
            const std::string pathBase = StripRaceGenderSuffix(pathStem);
            const std::string fileStem = FileNamePart(pathStem);
            const std::string fileBase = StripRaceGenderSuffix(fileStem);
            const HelmOffsetRule* best = nullptr;
            int bestScore = -1;
            for (const auto& rule : g_helmOffsetRules)
            {
                const int score = HelmRuleModelScore(rule, pathStem, pathBase, fileStem, fileBase, id);
                if (score > bestScore) { best = &rule; bestScore = score; }
            }
            if (!best) return true;
            if (best->disable) return false;
            if (best->add)
            {
                if (best->hasX) x += best->x;
                if (best->hasY) y += best->y;
                if (best->hasZ) z += best->z;
            }
            else
            {
                if (best->hasX) x = best->x;
                if (best->hasY) y = best->y;
                if (best->hasZ) z = best->z;
            }
            return true;
        }

        struct HelmOffsetDefault { std::string_view id; float x, z; };

        // Per-race/gender helm-bone offset, calibrated by eye against each race's head proportions.
        // A race/gender with no entry here falls back to the human default at the table's end.
        constexpr HelmOffsetDefault kHelmOffsetDefaults[] = {
            { "drf", -0.0587258f, -0.195f },
            { "drm", -0.0587258f, -0.245f },
            { "taf", -0.13f,      -0.1f },
            { "tam", -0.2f,       -0.1f },
            { "nim", -0.09f,      -0.18f },
            { "nif", -0.08f,      -0.195f },
            { "orf", -0.08f,      -0.171f },
            { "orm", -0.13f,      -0.21f },
            { "trf", -0.0887258f, -0.08623257f },
            { "trm", -0.13f,      -0.16f },
            { "bef",  0.01f,      -0.2f },
            { "bem", -0.08f,      -0.165f },
            { "huf", -0.09f,      -0.18f },
            { "scm", -0.12f,      -0.12623256f },
            { "scf", -0.01f,      -0.15f },
            { "gnf", -0.015f,     -0.263f },
            { "gnm", -0.009f,     -0.23f },
            { "dwm", -0.0227258f, -0.1725f },
            { "dwf",  0.01f,      -0.195f },
        };
        constexpr HelmOffsetDefault kHelmOffsetHumanDefault{ "", -0.0587258f, -0.18623257f };

        void HelmOffsetForId(const std::string& id, float& x, float& z)
        {
            const HelmOffsetDefault* found = &kHelmOffsetHumanDefault;
            for (const auto& entry : kHelmOffsetDefaults)
                if (entry.id == id) { found = &entry; break; }
            x = found->x;
            z = found->z;
        }
    }

    void FixObjectSkinTextureTypes(std::string_view name, std::vector<uint8_t>& model)
    {
        if (!tex::AllowsWeaponBladeRemap(name) || model.size() < sizeof(fmt::M2Header)) return;
        auto* md = reinterpret_cast<fmt::M2Header*>(model.data());
        const auto patched = tex::FixWeaponBladeTextureTypes(md, static_cast<uint32_t>(model.size()));
        if (patched.Total() && env::VerboseAssetLogs())
            WLOG_INFO("modern-assets: %.*s fixed WEAPON_BLADE textures objectSkin=%u hardcoded=%u",
                      int(name.size()), name.data(), patched.toObjectSkin, patched.toHardcoded);
    }

    bool ApplyHelmOffset(std::string_view name, std::vector<uint8_t>& model)
    {
        std::string id;
        if (!HelmRaceId(name, id) || model.size() < sizeof(fmt::M2Header)) return false;
        auto* md = reinterpret_cast<fmt::M2Header*>(model.data());
        if (md->magic != fmt::kMagicMD20 || !md->bones.count || !md->bones.offset) return false;
        const uint32_t boneCount = md->bones.count;
        const uint32_t boneOffset = md->bones.offset;
        const uint64_t bonesEnd = uint64_t(boneOffset) + uint64_t(boneCount) * sizeof(fmt::M2CompBone);
        if (bonesEnd > model.size() || bonesEnd < boneOffset ||
            uint64_t(model.size()) + uint64_t(boneCount) * sizeof(AppendedTranslationTrack) > 0xffffffffu)
            return false;

        float x, z;
        HelmOffsetForId(id, x, z);
        float y = 0.0f;
        if (!ApplyHelmOffsetRule(name, id, x, y, z)) return false;

        auto boneAt = [&](uint32_t i) {
            return reinterpret_cast<fmt::M2CompBone*>(model.data() + boneOffset + i * sizeof(fmt::M2CompBone));
        };

        for (uint32_t i = 0; i < boneCount; ++i)
        {
            boneAt(i)->flags |= fmt::kBoneFlagTransformed;

            const uint32_t trackOffset = static_cast<uint32_t>(model.size());
            model.resize(model.size() + sizeof(AppendedTranslationTrack), 0);
            // model.data() may have moved: re-derive both pointers after the resize.
            auto* bone = boneAt(i);
            auto* track = reinterpret_cast<AppendedTranslationTrack*>(model.data() + trackOffset);

            bone->translation.timestamps = { 1, trackOffset };
            track->innerTimestamps = { 1, trackOffset + offsetof(AppendedTranslationTrack, timestampValue) };

            bone->translation.values = { 1, trackOffset + offsetof(AppendedTranslationTrack, innerValues) };
            track->innerValues = { 1, trackOffset + offsetof(AppendedTranslationTrack, value) };
            track->value[0] = x; track->value[1] = y; track->value[2] = z;
        }
        if (env::VerboseAssetLogs())
            WLOG_INFO("modern-assets: %.*s applied helm offset id=%s x=%g y=%g z=%g bones=%u",
                      int(name.size()), name.data(), id.c_str(), x, y, z, boneCount);
        return true;
    }
}
