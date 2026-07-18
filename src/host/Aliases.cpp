// Texture and model name aliases: the alternate spellings the client may open the same asset under.
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

#include "Aliases.hpp"

#include "Produce.hpp"

#include <cctype>
#include <cstring>
#include <string_view>

namespace wxl::host::produce
{
    namespace
    {
        bool EndsWithCI(std::string_view s, const char* suffix)
        {
            const size_t ls = std::strlen(suffix);
            if (ls > s.size()) return false;
            for (size_t i = 0; i < ls; ++i)
            {
                const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[s.size() - ls + i])));
                const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
                if (a != b) return false;
            }
            return true;
        }

        bool StartsWithCI(std::string_view s, const char* prefix)
        {
            const size_t lp = std::strlen(prefix);
            if (lp > s.size()) return false;
            for (size_t i = 0; i < lp; ++i)
            {
                const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
                const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(prefix[i])));
                if (a != b) return false;
            }
            return true;
        }

        void AddUniqueAlias(std::vector<std::string>& aliases, const std::string& original, std::string alias)
        {
            if (alias.empty() || alias == original) return;
            for (const std::string& existing : aliases)
                if (existing == alias) return;
            aliases.emplace_back(std::move(alias));
        }

        void AppendTextureComponentAliases(const std::string& name, std::vector<std::string>& aliases)
        {
            const std::string key = NameKey(name);
            if (!StartsWithCI(key, "item\\texturecomponents\\")) return;
            if (!EndsWithCI(key, ".blp") && !EndsWithCI(key, ".tga")) return;

            const size_t dot = name.find_last_of('.');
            if (dot == std::string::npos || dot < 2) return;

            const char gender = static_cast<char>(std::tolower(static_cast<unsigned char>(name[dot - 1])));
            if (name[dot - 2] != '_' || (gender != 'f' && gender != 'm')) return;

            std::string unsuffixed = name;
            unsuffixed.erase(dot - 2, 2);

            const std::string stem = NameKey(std::string_view(name).substr(0, dot - 2));
            bool hasUnisexMarker = EndsWithCI(stem, "_u");
            const size_t u = stem.rfind("_u_");
            if (!hasUnisexMarker && u != std::string::npos)
            {
                hasUnisexMarker = true;
                for (size_t i = u + 3; i < stem.size(); ++i)
                {
                    if (stem[i] < '0' || stem[i] > '9')
                    {
                        hasUnisexMarker = false;
                        break;
                    }
                }
            }

            if (hasUnisexMarker)
                AddUniqueAlias(aliases, name, unsuffixed);
            else
            {
                std::string unisex = name;
                unisex[dot - 1] = 'u';
                AddUniqueAlias(aliases, name, std::move(unisex));
            }

            std::string opposite = name;
            opposite[dot - 1] = (gender == 'm') ? 'f' : 'm';
            AddUniqueAlias(aliases, name, std::move(opposite));
            AddUniqueAlias(aliases, name, std::move(unsuffixed));
        }

        void AppendSemicolonTextureAliases(const std::string& name, std::vector<std::string>& aliases)
        {
            if (!EndsWithCI(name, ".blp") && !EndsWithCI(name, ".tga")) return;

            const size_t dot = name.find_last_of('.');
            if (dot == std::string::npos) return;

            const size_t slash = name.find_last_of("\\/");
            const size_t fileStart = (slash == std::string::npos) ? 0 : slash + 1;
            const size_t semicolon = name.find(';', fileStart);
            if (semicolon == std::string::npos || semicolon >= dot) return;

            const std::string prefix = name.substr(0, fileStart);
            const std::string extension = name.substr(dot);
            const std::string key = NameKey(name);
            const bool objectComponent =
                StartsWithCI(key, "item\\objectcomponents\\") &&
                !StartsWithCI(key, "item\\objectcomponents\\collections\\");

            size_t partStart = fileStart;
            for (;;)
            {
                size_t rawEnd = name.find(';', partStart);
                if (rawEnd == std::string::npos || rawEnd > dot) rawEnd = dot;
                size_t partEnd = rawEnd;

                while (partStart < partEnd && (name[partStart] == ' ' || name[partStart] == '\t'))
                    ++partStart;
                while (partEnd > partStart && (name[partEnd - 1] == ' ' || name[partEnd - 1] == '\t'))
                    --partEnd;

                if (partEnd > partStart)
                {
                    const std::string stem = name.substr(partStart, partEnd - partStart);
                    AddUniqueAlias(aliases, name, prefix + stem + extension);
                    if (objectComponent)
                        AddUniqueAlias(aliases, name, std::string("Item\\ObjectComponents\\Collections\\") + stem + extension);
                }

                if (rawEnd >= dot) break;
                partStart = rawEnd + 1;
            }
        }

        void AppendObjectComponentRaceGenderAliases(const std::string& name, std::vector<std::string>& aliases)
        {
            const std::string key = NameKey(name);
            if (!StartsWithCI(key, "item\\objectcomponents\\")) return;
            if (!EndsWithCI(key, ".m2") && !EndsWithCI(key, ".mdx") && !EndsWithCI(key, ".skin")) return;

            size_t dot = key.find_last_of('.');
            if (dot == std::string::npos) return;

            size_t suffixEnd = dot;
            if (EndsWithCI(key, ".skin") && dot >= 2 &&
                key[dot - 1] >= '0' && key[dot - 1] <= '9' &&
                key[dot - 2] >= '0' && key[dot - 2] <= '9')
            {
                suffixEnd = dot - 2;
            }

            const size_t slash = key.find_last_of('\\');
            const size_t base = (slash == std::string::npos) ? 0 : slash + 1;
            auto isRace = [](char a, char b) {
                return a >= 'a' && a <= 'z' && b >= 'a' && b <= 'z';
            };
            auto isGender = [](char c) {
                return c == 'm' || c == 'f';
            };
            std::vector<std::string> candidates;
            candidates.emplace_back(name);
            auto addDerivedAlias = [&](std::string alias) {
                if (alias.empty() || alias == name) return;
                for (const std::string& candidate : candidates)
                    if (candidate == alias) return;
                for (const std::string& existing : aliases)
                    if (existing == alias) return;
                aliases.emplace_back(alias);
                candidates.emplace_back(std::move(alias));
            };

            if (suffixEnd >= base + 4)
            {
                const size_t s = suffixEnd - 4;
                if (s > base && key[s - 1] == '_' && isRace(key[s], key[s + 1]) &&
                    key[s + 2] == '_' && isGender(key[s + 3]))
                {
                    std::string alias = name;
                    alias.erase(s + 2, 1);
                    addDerivedAlias(std::move(alias));

                    alias = name;
                    alias.erase(s - 1, suffixEnd - (s - 1));
                    addDerivedAlias(std::move(alias));
                }
            }

            if (suffixEnd >= base + 3)
            {
                const size_t s = suffixEnd - 3;
                if (s > base && key[s - 1] == '_' && isRace(key[s], key[s + 1]) && isGender(key[s + 2]))
                {
                    std::string alias = name;
                    alias.insert(s + 2, 1, '_');
                    addDerivedAlias(std::move(alias));

                    alias = name;
                    alias.erase(s - 1, suffixEnd - (s - 1));
                    addDerivedAlias(std::move(alias));
                }
            }

            auto addModelExtensionAlias = [&](const std::string& candidate) {
                const std::string candidateKey = NameKey(candidate);
                const size_t ext = candidate.find_last_of('.');
                if (ext == std::string::npos) return;

                if (EndsWithCI(candidateKey, ".m2"))
                {
                    std::string alias = candidate;
                    alias.replace(ext, std::string::npos, ".mdx");
                    addDerivedAlias(std::move(alias));
                }
                else if (EndsWithCI(candidateKey, ".mdx"))
                {
                    std::string alias = candidate;
                    alias.replace(ext, std::string::npos, ".m2");
                    addDerivedAlias(std::move(alias));
                }
            };

            auto addCollectionAlias = [&](const std::string& candidate) {
                constexpr const char* kObjectPrefix = "item\\objectcomponents\\";
                constexpr const char* kCollectionsPrefix = "Item\\ObjectComponents\\Collections\\";

                const std::string candidateKey = NameKey(candidate);
                if (!StartsWithCI(candidateKey, kObjectPrefix)) return;
                if (StartsWithCI(candidateKey, "item\\objectcomponents\\collections\\")) return;
                if (StartsWithCI(candidateKey, "item\\objectcomponents\\collection\\")) return;

                const size_t folderStart = std::strlen(kObjectPrefix);
                const size_t fileStart = candidateKey.find('\\', folderStart);
                if (fileStart == std::string::npos || fileStart + 1 >= candidateKey.size()) return;
                const size_t stemEnd = candidateKey.find_last_of('.');
                if (stemEnd == std::string::npos || stemEnd <= fileStart + 1) return;

                std::string alias = candidate;
                alias.replace(0, fileStart + 1, kCollectionsPrefix);
                addDerivedAlias(std::move(alias));
            };

            for (size_t i = 0; i < candidates.size(); ++i)
            {
                addCollectionAlias(candidates[i]);
                addModelExtensionAlias(candidates[i]);
            }
        }

        void AppendObjectComponentTextureAliases(const std::string& name, std::vector<std::string>& aliases)
        {
            const std::string key = NameKey(name);
            if (!StartsWithCI(key, "item\\objectcomponents\\")) return;
            if (StartsWithCI(key, "item\\objectcomponents\\collections\\")) return;
            if (StartsWithCI(key, "item\\objectcomponents\\collection\\")) return;
            if (!EndsWithCI(key, ".blp") && !EndsWithCI(key, ".tga")) return;

            constexpr const char* kObjectPrefix = "item\\objectcomponents\\";
            constexpr const char* kCollectionsPrefix = "Item\\ObjectComponents\\Collections\\";
            const size_t folderStart = std::strlen(kObjectPrefix);
            const size_t fileStart = key.find('\\', folderStart);
            if (fileStart == std::string::npos || fileStart + 1 >= key.size()) return;
            const size_t stemEnd = key.find_last_of('.');
            if (stemEnd == std::string::npos || stemEnd <= fileStart + 1) return;

            std::string alias = name;
            alias.replace(0, fileStart + 1, kCollectionsPrefix);
            AddUniqueAlias(aliases, name, std::move(alias));

            constexpr const char* kCollectionPrefix = "Item\\ObjectComponents\\Collection\\";
            alias = name;
            alias.replace(0, fileStart + 1, kCollectionPrefix);
            AddUniqueAlias(aliases, name, std::move(alias));
        }
    }

    void BuildAliases(const std::string& name, std::vector<std::string>& aliases)
    {
        AppendSemicolonTextureAliases(name, aliases);
        AppendObjectComponentRaceGenderAliases(name, aliases);
        AppendObjectComponentTextureAliases(name, aliases);
        AppendTextureComponentAliases(name, aliases);
    }
}
