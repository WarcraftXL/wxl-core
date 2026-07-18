// Dev-mode hot reload: polls the extensions folder and signals a full VM reload on change.
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

#include "engine/lua/DevReload.hpp"

#include "engine/lua/Loader.hpp"
#include "common/Config.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wxl::lua::dev
{
    namespace
    {
        uint64_t g_signature    = 0;
        bool     g_haveBaseline = false;
        uint64_t g_lastPollMs   = 0;

        // A single FNV-1a fold over every loadable file's name, last-write time and size. Any add,
        // remove, rename, edit or touch changes the digest; a rename to the same bytes still moves
        // it because the name folds in. Cheap enough to run a few times a second.
        uint64_t ScanSignature()
        {
            char dir[MAX_PATH];
            if (!loader::ResolveDir(dir, sizeof(dir)))
                return 0;

            char pattern[MAX_PATH];
            std::snprintf(pattern, sizeof(pattern), "%s\\*", dir);
            WIN32_FIND_DATAA fd;
            HANDLE          h = FindFirstFileA(pattern, &fd);
            if (h == INVALID_HANDLE_VALUE)
                return 0;

            struct FileStamp
            {
                std::string name;
                uint64_t writeTime;
                uint64_t size;
            };
            std::vector<FileStamp> files;

            do
            {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    continue;
                const char* dot = std::strrchr(fd.cFileName, '.');
                if (!dot || (_stricmp(dot, ".lua") != 0 && _stricmp(dot, ".out") != 0))
                    continue;
                files.push_back({
                    fd.cFileName,
                    (static_cast<uint64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32) |
                        fd.ftLastWriteTime.dwLowDateTime,
                    (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow,
                });
            } while (FindNextFileA(h, &fd));
            FindClose(h);

            // FindFirstFile enumeration order is not stable. Hash a deterministic order so an unchanged
            // extension set cannot spuriously reload merely because Windows returned its entries differently.
            std::sort(files.begin(), files.end(), [](const FileStamp& a, const FileStamp& b) {
                const int insensitive = _stricmp(a.name.c_str(), b.name.c_str());
                return insensitive != 0 ? insensitive < 0 : a.name < b.name;
            });

            uint64_t           sig   = 1469598103934665603ULL; // FNV-1a offset basis
            constexpr uint64_t kPrime = 1099511628211ULL;
            auto               fold  = [&](uint64_t v) { sig ^= v; sig *= kPrime; };
            for (const FileStamp& file : files)
            {
                for (const char* p = file.name.c_str(); *p; ++p)
                    fold(static_cast<uint8_t>(*p));
                fold(file.writeTime);
                fold(file.size);
            }
            return sig;
        }
    } // namespace

    bool Enabled()
    {
        return wxl::config::Env("WXL_DEV_MODE", false);
    }

    void Snapshot()
    {
        g_signature    = ScanSignature();
        g_haveBaseline = true;
        g_lastPollMs   = GetTickCount64();
    }

    bool PollChanged()
    {
        const uint64_t now = GetTickCount64();
        if (g_haveBaseline && now - g_lastPollMs < 500)
            return false; // rate-limit disk scans to at most ~2/s
        g_lastPollMs = now;

        const uint64_t sig = ScanSignature();
        if (!g_haveBaseline)
        {
            g_signature    = sig;
            g_haveBaseline = true;
            return false;
        }
        if (sig != g_signature)
        {
            g_signature = sig;
            return true;
        }
        return false;
    }
}
