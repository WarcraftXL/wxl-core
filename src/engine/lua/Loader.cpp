// Extension discovery and loading: finds <exe>/extensions and runs each .lua / .out in order.
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

#include "engine/lua/Loader.hpp"

#include "engine/lua/LuaJit.hpp"
#include "engine/lua/DevReload.hpp"
#include "engine/security/Manifest.hpp"
#include "common/Config.hpp"
#include "common/Log.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <string.h>
#include <string>
#include <vector>

namespace wxl::lua::loader
{
    bool ResolveDir(char* buf, size_t cap)
    {
        if (wxl::config::Raw("WXL_EXTENSIONS_DIR", buf, cap))
            return true;

        // Default: a folder named "extensions" next to the running Wow.exe.
        char exe[MAX_PATH];
        const DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return false;
        char* slash = std::strrchr(exe, '\\');
        if (!slash)
            return false;
        *slash = '\0';
        const int w = std::snprintf(buf, cap, "%s\\extensions", exe);
        return w > 0 && static_cast<size_t>(w) < cap;
    }

    namespace
    {
        // Widens a system-codepage path (as produced by GetModuleFileNameA / FindFirstFileA) to the
        // wide form the security layer's Win32 file reads expect. Returns an empty string on failure.
        std::wstring Widen(const char* s)
        {
            const int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
            if (n <= 0)
                return std::wstring();
            std::wstring w(static_cast<size_t>(n - 1), L'\0');
            MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
            return w;
        }

        // True for a compiled-bytecode extension (.out): its LuaJIT-version requirement is a hard
        // rule (ABI-bound), unlike .lua source which this engine recompiles.
        bool IsCompiled(const std::string& name)
        {
            const char* dot = std::strrchr(name.c_str(), '.');
            return dot && _stricmp(dot, ".out") == 0;
        }
    } // namespace

    int LoadAll(lua_State* L)
    {
        char dir[MAX_PATH];
        if (!ResolveDir(dir, sizeof(dir)))
        {
            WLOG_WARN("[vm] could not resolve the extensions directory");
            return 0;
        }
        WLOG_DEBUG("[vm] extensions directory: %s", dir);

        // Collect the loadable file names first, then sort, so load order is deterministic and
        // independent of the filesystem's enumeration order.
        std::vector<std::string> files;
        {
            char pattern[MAX_PATH];
            std::snprintf(pattern, sizeof(pattern), "%s\\*", dir);
            WIN32_FIND_DATAA fd;
            HANDLE          h = FindFirstFileA(pattern, &fd);
            if (h == INVALID_HANDLE_VALUE)
            {
                WLOG_INFO("[vm] no extensions directory at %s", dir);
                return 0;
            }
            do
            {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    continue;
                const char* dot = std::strrchr(fd.cFileName, '.');
                if (!dot)
                    continue;
                if (_stricmp(dot, ".lua") == 0 || _stricmp(dot, ".out") == 0)
                    files.emplace_back(fd.cFileName);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        std::sort(files.begin(), files.end());

        // Trust gate. Dev mode loads everything unsigned; production requires a manifest that verifies
        // against the embedded key or NOTHING loads (fail closed, docs/plan-v1.1.md §4).
        const bool           dev = dev::Enabled();
        security::Manifest   manifest;
        bool                 versionOk = true;
        if (dev)
        {
            WLOG_WARN("[vm] dev mode: extension signature checks bypassed");
        }
        else
        {
            std::string err;
            if (!security::LoadAndVerify(Widen(dir), manifest, err))
            {
                WLOG_ERROR("[vm] extension manifest rejected, loading nothing: %s", err.c_str());
                return 0;
            }
            versionOk = security::LuaJitVersionMatches(manifest);
            if (!versionOk)
                WLOG_WARN("[vm] manifest built for LuaJIT '%s'; this engine differs",
                          manifest.luajitVersion.c_str());
        }

        int loaded = 0;
        for (const std::string& f : files)
        {
            char path[MAX_PATH];
            std::snprintf(path, sizeof(path), "%s\\%s", dir, f.c_str());

            if (!dev)
            {
                std::string err;
                if (!security::VerifyFile(manifest, Widen(path), f, err))
                {
                    WLOG_WARN("[vm] skipping %s: %s", f.c_str(), err.c_str());
                    continue;
                }
                // A LuaJIT-version mismatch rejects compiled bytecode outright (ABI-bound) but only
                // warns for .lua source, which this engine recompiles from text.
                if (!versionOk && IsCompiled(f))
                {
                    WLOG_WARN("[vm] skipping %s: compiled for a different LuaJIT", f.c_str());
                    continue;
                }
            }
            // luaL_loadfile pushes either the compiled chunk or an error string; a failed pcall
            // pushes the runtime error. Either way the stack is restored before the next file so a
            // failing extension cannot leak state onto the shared stack.
            if (luaL_loadfile(L, path) != 0)
            {
                WLOG_ERROR("[vm] compile failed %s: %s", f.c_str(), lua_tostring(L, -1));
                lua_pop(L, 1);
                continue;
            }
            if (lua_pcall(L, 0, 0, 0) != 0)
            {
                WLOG_ERROR("[vm] run failed %s: %s", f.c_str(), lua_tostring(L, -1));
                lua_pop(L, 1);
                continue;
            }
            WLOG_DEBUG("[vm] loaded extension %s", f.c_str());
            ++loaded;
        }
        WLOG_INFO("[vm] loaded %d/%zu %sextension(s) from %s", loaded, files.size(),
                  dev ? "" : "verified ", dir);
        return loaded;
    }
}
