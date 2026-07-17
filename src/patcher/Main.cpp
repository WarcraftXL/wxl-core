// wxl-patcher: add the WarcraftXL import to the client PE and run every registered PatchScript.
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

#include "patcher/PatchScript.hpp"
#include "patcher/PeImage.hpp"

#include "common/Log.hpp"

#include <windows.h>
#include <io.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    constexpr char kDllName[]  = "WarcraftXL.dll";
    constexpr char kFuncName[] = "WarcraftXL";
    constexpr char kTagSection[] = ".wxl";

    /**
     * @brief Reads an entire file into a byte buffer.
     * @param path  file path to read.
     * @return File contents, or an empty buffer on failure.
     */
    std::vector<uint8_t> ReadAll(const char* path)
    {
        std::vector<uint8_t> data;
        FILE* f = nullptr;
        if (fopen_s(&f, path, "rb") != 0 || !f) return data;
        long size = -1;
        if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
        if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return data; }
        data.resize(static_cast<size_t>(size));
        if (fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
        fclose(f);
        return data;
    }

    /**
     * @brief Writes a byte buffer to a file atomically: the bytes go to a sibling temp file,
     *        flushed to disk, then swapped into place. The target is either fully replaced or
     *        untouched — a crash or short write can never leave it truncated.
     * @param path  file path to write.
     * @param data  bytes to write.
     * @return True on success, false on any I/O failure (the temp file is removed).
     */
    bool WriteAll(const char* path, const std::vector<uint8_t>& data)
    {
        const std::string tmp = std::string(path) + ".wxltmp";
        FILE* f = nullptr;
        if (fopen_s(&f, tmp.c_str(), "wb") != 0 || !f) return false;
        const bool written = fwrite(data.data(), 1, data.size(), f) == data.size()
                          && fflush(f) == 0
                          && _commit(_fileno(f)) == 0;
        const bool closed = fclose(f) == 0;
        if (!written || !closed) { remove(tmp.c_str()); return false; }
        if (!MoveFileExA(tmp.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        { remove(tmp.c_str()); return false; }
        return true;
    }
}

/**
 * @brief Patches the target PE: sets large-address-aware, runs every registered PatchScript, and adds the
 *        WarcraftXL import, writing a backup of the original.
 * @param argc  argument count.
 * @param argv  argument values; argv[1] is the target path, defaulting to "Wow.exe".
 * @return 0 on success, 1 on failure.
 */
int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    wxl::log::EnableConsole("[WarcraftXL] "); // progress to stdout, failures to stderr
    const char* target = argc > 1 ? argv[1] : "Wow.exe";
    WLOG_INFO("patcher start, target='%s'", target);

    std::vector<uint8_t> file = ReadAll(target);
    if (file.empty()) { WLOG_ERROR("cannot read '%s'", target); return 1; }

    wxl::patcher::PeImage pe(file);
    if (!pe.valid()) { WLOG_ERROR("'%s' is not a 32-bit PE", target); return 1; }

    // Always ensure 4 GB address space, even when an older patcher already injected the .wxl section.
    pe.SetLargeAddressAware();
    if (pe.HasSection(kTagSection))
    {
        if (!WriteAll(target, file)) { WLOG_ERROR("cannot write '%s'", target); return 1; }
        WLOG_INFO("already patched ('%s' present), ensured large-address-aware", kTagSection);
        return 0;
    }

    // Apply every registered PatchScript (byte edits), then the import (structural).
    for (wxl::patcher::PatchScript* const s : wxl::patcher::registry::Scripts())
    {
        if (!s->Apply(pe))
        { WLOG_ERROR("patch-script '%s' FAILED", s->name()); return 1; }
        WLOG_INFO("applied patch-script '%s'", s->name());
    }

    if (!pe.AddImport(kDllName, kFuncName, kTagSection))
    { WLOG_ERROR("import injection failed"); return 1; }

    // The backup is the only recovery path if the patched image misbehaves: refuse to touch the
    // target until it is confirmed on disk.
    std::string backup = std::string(target) + ".orig";
    if (GetFileAttributesA(backup.c_str()) == INVALID_FILE_ATTRIBUTES
        && !CopyFileA(target, backup.c_str(), TRUE))
    {
        WLOG_ERROR("cannot back up '%s' to '%s' (win32=%lu), aborting before write",
                   target, backup.c_str(), GetLastError());
        return 1;
    }

    if (!WriteAll(target, file)) { WLOG_ERROR("cannot write '%s'", target); return 1; }

    WLOG_INFO("patched '%s' (+import %s!%s, %zu patch-scripts, backup '%s')",
              target, kDllName, kFuncName, wxl::patcher::registry::Scripts().size(), backup.c_str());
    return 0;
}
