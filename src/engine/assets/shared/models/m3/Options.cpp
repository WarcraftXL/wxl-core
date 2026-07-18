// Sidecar options parsing for drop-in model sources.
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

#include "Options.hpp"

#include <algorithm>
#include <cctype>

namespace wxl::modern::assets::m3
{
    namespace
    {
        std::string Trim(const std::string& s)
        {
            size_t a = 0, b = s.size();
            while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
            return s.substr(a, b - a);
        }
    }

    std::string NormalizePath(std::string s)
    {
        std::replace(s.begin(), s.end(), '/', '\\');
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        return s;
    }

    void ParseOptions(const std::string& text, ModelSource& src)
    {
        size_t pos = 0;
        while (pos <= text.size())
        {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            const std::string line = Trim(text.substr(pos, end - pos));
            pos = end + 1;
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = Trim(line.substr(0, eq));
            const std::string val = Trim(line.substr(eq + 1));
            if (key == "m3a")          src.m3a = val;
            else if (key == "map")     src.map = val;
            else if (key == "tex")     src.tex = val;
            else if (key == "texroot") src.texroot = val;
            else if (key == "lift")    src.lift = std::stof(val);
            else if (key == "ribmesh") src.ribmesh = (val != "0");
            else if (key == "ribtilt") src.ribtilt = std::stof(val);
            else if (key == "riblen")  src.riblen = std::stof(val);
            else if (key == "ribbasewidth") src.ribbasewidth = std::stof(val);
            else if (key == "ambient") src.ambient = val;
            else if (key == "parcolor")
            {
                size_t p = 0;
                for (int c = 0; c < 3 && p != std::string::npos; ++c)
                {
                    src.parcolor[c] = std::stof(val.substr(p));
                    p = val.find(',', p);
                    if (p != std::string::npos) ++p;
                }
            }
        }
    }
}
