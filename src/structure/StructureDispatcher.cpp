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

#include "StructureDispatcher.hpp"

#include <cctype>

namespace
{
    bool EndsWithCI(std::string_view s, std::string_view suffix)
    {
        if (suffix.size() > s.size()) return false;
        size_t base = s.size() - suffix.size();
        for (size_t i = 0; i < suffix.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(s[base + i])) != suffix[i]) return false;
        return true;
    }

    // A group file is "<root>_NNN.wmo" (three decimal digits before the extension); the root is "<name>.wmo".
    bool IsWmoGroup(std::string_view name)
    {
        if (!EndsWithCI(name, ".wmo") || name.size() < 8) return false;
        size_t dot = name.size() - 4; // index of '.' in ".wmo"
        return name[dot - 4] == '_' &&
               std::isdigit(static_cast<unsigned char>(name[dot - 3])) &&
               std::isdigit(static_cast<unsigned char>(name[dot - 2])) &&
               std::isdigit(static_cast<unsigned char>(name[dot - 1]));
    }
}

namespace wraith::structure
{
    Format Classify(std::string_view name)
    {
        if (EndsWithCI(name, ".wmo")) return Format::Wmo;
        if (EndsWithCI(name, ".adt")) return Format::Adt;
        if (EndsWithCI(name, ".wdt")) return Format::Wdt;
        if (EndsWithCI(name, ".wdl")) return Format::Wdl;
        if (EndsWithCI(name, ".m2") || EndsWithCI(name, ".mdx")) return Format::M2;
        return Format::Raw;
    }

    // Cold router: once per file open, pick the concrete translator. Single-input formats
    // (Wmo/Wdt/Wdl/M2) transform `in` in place to `out`. ADT is a MULTI-input merge (root +
    // _tex0 + _obj0) and is orchestrated by the host caller, not here, so it returns false.
    // A false return means "not translated" and the caller serves the raw `in` bytes.
    bool Dispatch(std::string_view name, std::span<const uint8_t> in, const ResolveCtx& rc,
                  std::vector<uint8_t>& out)
    {
        switch (Classify(name))
        {
        case Format::Wmo:
            return IsWmoGroup(name) ? wmo::TranslateWmoGroup(in, out)
                                    : wmo::TranslateWmoRoot(in, rc, out);
        case Format::Wdt:
            return wdt::FixWdt(in, out);
        case Format::Wdl:
            return wdl::RebuildWdl(in, out);
        case Format::M2:
            return m2::TranslateM2(in, rc, out);
        case Format::Adt:
        case Format::Raw:
        default:
            (void)in; (void)out;
            return false;
        }
    }
}
