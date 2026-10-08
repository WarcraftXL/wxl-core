// Named profiles over every registered settings set: WTF\WarcraftXL\profiles\<name>.cfg.
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
//
// The file: '#' comments, a [set] line per set, then key=value lines -- true / false for a boolean,
// a number, or text. Every set registered is written, whoever registered it, so a profile carries
// the foliage, its look, the post-process stack and whatever comes next without this file knowing.

#include "engine/script/Profiles.hpp"

#include "engine/script/LuaSettings.hpp"

#include "common/Config.hpp"
#include "common/Log.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    constexpr const char* kDir = "WTF\\WarcraftXL\\profiles";

    std::string g_current;

    std::string PathOf(const char* name)
    {
        return std::string(kDir) + "\\" + name + ".cfg";
    }

    void Trim(std::string& s)
    {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        size_t i = 0;
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        s.erase(0, i);
    }

    void Write(std::FILE* f, const char* key, const WXL_SettingValue& v)
    {
        switch (v.type)
        {
            case WXL_SETTING_BOOLEAN: std::fprintf(f, "%s=%s\r\n", key, v.boolean ? "true" : "false"); break;
            case WXL_SETTING_NUMBER:  std::fprintf(f, "%s=%.6g\r\n", key, v.number); break;
            case WXL_SETTING_STRING:  std::fprintf(f, "%s=%s\r\n", key, v.string ? v.string : ""); break;
            default: break;
        }
    }

    /// Text back to a value: true / false, a number, else text.
    WXL_SettingValue Parse(const std::string& text)
    {
        WXL_SettingValue v = {};
        if (_stricmp(text.c_str(), "true") == 0 || _stricmp(text.c_str(), "false") == 0)
        {
            v.type = WXL_SETTING_BOOLEAN;
            v.boolean = _stricmp(text.c_str(), "true") == 0 ? 1 : 0;
            return v;
        }
        char* end = nullptr;
        const double n = std::strtod(text.c_str(), &end);
        if (end != text.c_str() && *end == '\0')
        {
            v.type = WXL_SETTING_NUMBER;
            v.number = n;
            return v;
        }
        v.type = WXL_SETTING_STRING;
        v.string = text.c_str();
        return v;
    }
}

namespace wxl::script::profiles
{
    bool ValidName(const char* name)
    {
        if (!name || !*name || name[0] == '.') return false;
        const size_t n = std::strlen(name);
        if (n > 48) return false;
        for (size_t i = 0; i < n; ++i)
        {
            const unsigned char c = static_cast<unsigned char>(name[i]);
            if (!(std::isalnum(c) || c == ' ' || c == '-' || c == '_' || c == '.')) return false;
        }
        return name[n - 1] != ' ' && name[n - 1] != '.';
    }

    bool Save(const char* name)
    {
        if (!ValidName(name)) return false;
        CreateDirectoryA("WTF", nullptr);
        CreateDirectoryA("WTF\\WarcraftXL", nullptr);
        CreateDirectoryA(kDir, nullptr);
        std::FILE* f = nullptr;
        if (fopen_s(&f, PathOf(name).c_str(), "wb") != 0 || !f)
        {
            WLOG_WARN("profiles: %s could not be written", PathOf(name).c_str());
            return false;
        }
        std::fprintf(f, "# WarcraftXL profile \"%s\", written by the game.\r\n", name);
        uint32_t keys = 0;
        for (uint32_t s = 0; s < SettingsCount(); ++s)
        {
            const WXL_SettingsSet* set = SettingsAt(s);
            std::fprintf(f, "\r\n[%s]\r\n", set->name);
            for (uint32_t k = 0; k < set->keyCount; ++k)
            {
                WXL_SettingValue v = {};
                if (!set->keys[k] || !set->Get(set->user, set->keys[k], &v)) continue;
                Write(f, set->keys[k], v);
                ++keys;
            }
        }
        std::fclose(f);
        g_current = name;
        WLOG_INFO("profiles: \"%s\" saved (%u set(s), %u value(s))", name, SettingsCount(), keys);
        return true;
    }

    bool Load(const char* name)
    {
        if (!ValidName(name)) return false;
        std::FILE* f = nullptr;
        if (fopen_s(&f, PathOf(name).c_str(), "rb") != 0 || !f) return false;
        const WXL_SettingsSet* set = nullptr;
        uint32_t applied = 0, skipped = 0;
        char line[512];
        while (std::fgets(line, sizeof line, f))
        {
            std::string s(line);
            Trim(s);
            if (s.empty() || s[0] == '#' || s[0] == ';') continue;
            if (s.front() == '[' && s.back() == ']')
            {
                const std::string setName = s.substr(1, s.size() - 2);
                set = nullptr;
                for (uint32_t i = 0; i < SettingsCount() && !set; ++i)
                    if (std::strcmp(SettingsAt(i)->name, setName.c_str()) == 0) set = SettingsAt(i);
                continue;
            }
            const size_t eq = s.find('=');
            if (!set || !set->Set || eq == std::string::npos)
            {
                ++skipped;
                continue;
            }
            std::string key = s.substr(0, eq), value = s.substr(eq + 1);
            Trim(key);
            Trim(value);
            const WXL_SettingValue v = Parse(value);
            if (set->Set(set->user, key.c_str(), &v)) ++applied;
            else ++skipped;
        }
        std::fclose(f);
        g_current = name;
        WLOG_INFO("profiles: \"%s\" loaded (%u value(s) applied, %u skipped)", name, applied, skipped);
        return true;
    }

    bool Delete(const char* name)
    {
        if (!ValidName(name)) return false;
        const bool ok = DeleteFileA(PathOf(name).c_str()) != 0;
        if (ok && g_current == name) g_current.clear();
        return ok;
    }

    std::vector<std::string> List()
    {
        std::vector<std::string> out;
        WIN32_FIND_DATAA data;
        const HANDLE h = FindFirstFileA((std::string(kDir) + "\\*.cfg").c_str(), &data);
        if (h == INVALID_HANDLE_VALUE) return out;
        do
        {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::string n(data.cFileName);
            if (n.size() > 4) n.resize(n.size() - 4);
            if (ValidName(n.c_str())) out.push_back(n);
        } while (FindNextFileA(h, &data));
        FindClose(h);
        std::sort(out.begin(), out.end());
        return out;
    }

    const char* Current() { return g_current.c_str(); }

    void LoadStartupOnce()
    {
        static bool done = false;
        if (done) return;
        done = true;
        char name[64] = {};
        if (!wxl::config::Raw("WXL_PROFILE", name, sizeof name)) return;
        if (!Load(name)) WLOG_WARN("profiles: WXL_PROFILE=%s could not be loaded", name);
    }
}
