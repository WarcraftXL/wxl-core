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

#include <windows.h>
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
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        data.resize(size);
        fread(data.data(), 1, size, f);
        fclose(f);
        return data;
    }

    /**
     * @brief Writes a byte buffer to a file, overwriting any existing contents.
     * @param path  file path to write.
     * @param data  bytes to write.
     * @return True on success, false if the file cannot be opened.
     */
    bool WriteAll(const char* path, const std::vector<uint8_t>& data)
    {
        FILE* f = nullptr;
        if (fopen_s(&f, path, "wb") != 0 || !f) return false;
        fwrite(data.data(), 1, data.size(), f);
        fclose(f);
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
    const char* target = argc > 1 ? argv[1] : "Wow.exe";
    printf("[WarcraftXL] patcher start, target='%s'\n", target);

    std::vector<uint8_t> file = ReadAll(target);
    if (file.empty()) { printf("[WarcraftXL] cannot read '%s'\n", target); return 1; }

    wxl::patcher::PeImage pe(file);
    if (!pe.valid()) { printf("[WarcraftXL] not a 32-bit PE\n"); return 1; }
    if (pe.HasSection(kTagSection))
    { printf("[WarcraftXL] already patched ('%s' present)\n", kTagSection); return 0; }

    // 4 GB address space, then every registered PatchScript (byte edits), then the import (structural).
    pe.SetLargeAddressAware();

    for (wxl::patcher::PatchScript* const s : wxl::patcher::registry::Scripts())
    {
        if (!s->Apply(pe))
        { printf("[WarcraftXL] patch-script '%s' FAILED\n", s->name()); return 1; }
        printf("[WarcraftXL] applied patch-script '%s'\n", s->name());
    }

    if (!pe.AddImport(kDllName, kFuncName, kTagSection))
    { printf("[WarcraftXL] import injection failed\n"); return 1; }

    std::string backup = std::string(target) + ".orig";
    if (GetFileAttributesA(backup.c_str()) == INVALID_FILE_ATTRIBUTES)
        CopyFileA(target, backup.c_str(), TRUE);

    if (!WriteAll(target, file)) { printf("[WarcraftXL] cannot write '%s'\n", target); return 1; }

    printf("[WarcraftXL] patched '%s' (+import %s!%s, %zu patch-scripts, backup '%s')\n",
           target, kDllName, kFuncName, wxl::patcher::registry::Scripts().size(), backup.c_str());
    return 0;
}
