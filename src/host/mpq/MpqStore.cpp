// MpqStore: asset-agnostic archive I/O over StormLib that serves raw bytes.
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

#include "mpq/MpqStore.hpp"

#include "core/Logger.hpp"

#include <StormLib.h>

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <fstream>

namespace
{
    /**
     * @brief Reports whether `path` names an existing file on disk (not a directory).
     * @param path  absolute file path
     * @return true if the file exists
     */
    bool FileExistsOnDisk(const std::string& path)
    {
        DWORD a = GetFileAttributesA(path.c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    }

    /**
     * @brief Returns a lowercased copy of `s`.
     * @param s  input string
     * @return lowercased string
     */
    std::string ToLower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    /**
     * @brief Reshapes a client file name to the archive-internal form: backslashes, no leading separators.
     * @param name  file name from the client (either separator)
     * @return archive-internal name
     */
    std::string NormalizeName(std::string_view name)
    {
        std::string n(name);
        for (char& c : n) if (c == '/') c = '\\';
        size_t i = 0;
        while (i < n.size() && n[i] == '\\') ++i;
        return n.substr(i);
    }
}

namespace wxl::host::mpq
{
    namespace log = wxl::core::log;

    /** @brief Closes all open archive handles. */
    MpqStore::~MpqStore()
    {
        for (void* h : m_archives) if (h) SFileCloseArchive(static_cast<HANDLE>(h));
        m_archives.clear();
    }

    /**
     * @brief Mounts the locale and base archives plus loose override folders under the data root.
     * @param dataDir  client data root
     * @return true if at least one archive or loose root mounted
     */
    bool MpqStore::Mount(std::string_view dataDir)
    {
        std::string root(dataDir);
        if (!root.empty() && (root.back() == '\\' || root.back() == '/')) root.pop_back();
        const std::string data = root + "\\Data";

        // One pass over Data\* finds both: the locale folder (carries locale-<loc>.MPQ) and the loose
        // override folders (Data\Patch*.MPQ that are DIRECTORIES, highest priority).
        m_locale.clear();
        std::vector<std::string> looseDirs;
        {
            WIN32_FIND_DATAA fd{};
            HANDLE h = FindFirstFileA((data + "\\*").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                    std::string d = fd.cFileName;
                    if (d == "." || d == "..") continue;
                    if (m_locale.empty() && FileExistsOnDisk(data + "\\" + d + "\\locale-" + d + ".MPQ"))
                        m_locale = d;
                    std::string dl = ToLower(d);
                    if (dl.rfind("patch", 0) == 0 && dl.size() > 4 && dl.substr(dl.size() - 4) == ".mpq")
                        looseDirs.push_back(d);
                } while (FindNextFileA(h, &fd));
                FindClose(h);
            }
        }
        const std::string loc = m_locale;

        std::sort(looseDirs.begin(), looseDirs.end(), [](const std::string& a, const std::string& b) {
            return ToLower(a) > ToLower(b); // Patch-5 before Patch-4 ...
        });
        for (const std::string& d : looseDirs) m_looseRoots.push_back(data + "\\" + d + "\\");

        // Archive set, highest priority first (search order).
        std::vector<std::string> candidates = {
            "Data\\" + loc + "\\patch-" + loc + "-3.MPQ", "Data\\patch-3.MPQ",
            "Data\\" + loc + "\\patch-" + loc + "-2.MPQ", "Data\\patch-2.MPQ",
            "Data\\" + loc + "\\patch-" + loc + ".MPQ",   "Data\\patch.MPQ",
            "Data\\" + loc + "\\locale-" + loc + ".MPQ",
            "Data\\" + loc + "\\base-" + loc + ".MPQ",
            "Data\\" + loc + "\\expansion-locale-" + loc + ".MPQ",
            "Data\\" + loc + "\\lichking-locale-" + loc + ".MPQ",
            "Data\\" + loc + "\\speech-" + loc + ".MPQ",
            "Data\\" + loc + "\\expansion-speech-" + loc + ".MPQ",
            "Data\\" + loc + "\\lichking-speech-" + loc + ".MPQ",
            "Data\\" + loc + "\\backup-" + loc + ".MPQ",
            "Data\\lichking.MPQ",
            "Data\\expansion.MPQ",
            "Data\\common-2.MPQ",
            "Data\\common.MPQ",
        };

        // Resolve by exact name only (never enumerate); skip the internal (listfile) and (attributes).
        const DWORD openFlags = MPQ_OPEN_READ_ONLY | MPQ_OPEN_NO_LISTFILE | MPQ_OPEN_NO_ATTRIBUTES;

        const ULONGLONG t0 = GetTickCount64();
        for (const std::string& rel : candidates)
        {
            std::string full = root + "\\" + rel;
            if (!FileExistsOnDisk(full)) continue;
            HANDLE hMpq = nullptr;
            if (SFileOpenArchive(full.c_str(), 0, openFlags, &hMpq) && hMpq)
            {
                m_archives.push_back(hMpq);
                m_archiveNames.push_back(rel);
            }
            else
            {
                log::Printf("mpq: open failed (%lu) %s", GetLastError(), rel.c_str());
            }
        }
        const ULONGLONG mountMs = GetTickCount64() - t0;

        log::Printf("mpq: locale=%s, %zu archives, %zu loose roots, mounted in %llu ms",
            m_locale.empty() ? "(none)" : m_locale.c_str(), m_archives.size(), m_looseRoots.size(),
            static_cast<unsigned long long>(mountMs));
        for (size_t i = 0; i < m_archiveNames.size(); ++i)
            log::Printf("mpq:   [%zu] %s", i, m_archiveNames[i].c_str());
        for (const std::string& lr : m_looseRoots)
            log::Printf("mpq:   loose <- %s", lr.c_str());

        return !m_archives.empty() || !m_looseRoots.empty();
    }

    /**
     * @brief Reports whether the file exists in any loose root or mounted archive.
     * @param rawName  file name from the client
     * @return true if the file is present
     */
    bool MpqStore::Exists(std::string_view rawName) const
    {
        const std::string name = NormalizeName(rawName);
        for (const std::string& lr : m_looseRoots)
            if (FileExistsOnDisk(lr + name)) return true;
        for (void* a : m_archives)
            if (SFileHasFile(static_cast<HANDLE>(a), name.c_str())) return true;
        return false;
    }

    /**
     * @brief Reads the whole file, preferring loose override folders over the archives.
     * @param rawName  file name from the client
     * @param out      receives the file bytes
     * @return false if the file is absent
     */
    bool MpqStore::ReadAll(std::string_view rawName, std::vector<uint8_t>& out) const
    {
        const std::string name = NormalizeName(rawName);

        // Loose override folders win over the archives.
        for (const std::string& lr : m_looseRoots)
        {
            std::ifstream f(lr + name, std::ios::binary | std::ios::ate);
            if (!f) continue;
            std::streamoff size = f.tellg();
            f.seekg(0);
            out.resize(static_cast<size_t>(size));
            if (size) f.read(reinterpret_cast<char*>(out.data()), size);
            return true;
        }

        for (void* a : m_archives)
        {
            HANDLE hFile = nullptr;
            if (!SFileOpenFileEx(static_cast<HANDLE>(a), name.c_str(), 0, &hFile) || !hFile) continue;
            DWORD high = 0;
            DWORD sz = SFileGetFileSize(hFile, &high);
            if (sz == SFILE_INVALID_SIZE) { SFileCloseFile(hFile); continue; }
            out.resize(sz);
            if (sz)
            {
                DWORD read = 0;
                SFileReadFile(hFile, out.data(), sz, &read, nullptr); // FALSE at exact EOF is fine
            }
            SFileCloseFile(hFile);
            return true;
        }
        return false;
    }

    /**
     * @brief Reads a byte range, preferring loose override folders over the archives.
     * @param rawName  file name from the client
     * @param off      start offset
     * @param len      maximum number of bytes to read
     * @param out      receives the bytes read (clamped to file end)
     * @return false if the file is absent
     */
    bool MpqStore::ReadRange(std::string_view rawName, uint32_t off, uint32_t len,
                             std::vector<uint8_t>& out) const
    {
        const std::string name = NormalizeName(rawName);

        for (const std::string& lr : m_looseRoots)
        {
            std::ifstream f(lr + name, std::ios::binary | std::ios::ate);
            if (!f) continue;
            uint32_t size = static_cast<uint32_t>(f.tellg());
            if (off >= size) { out.clear(); return true; }
            if (len > size - off) len = size - off;
            out.resize(len);
            f.seekg(off);
            if (len) f.read(reinterpret_cast<char*>(out.data()), len);
            out.resize(static_cast<size_t>(f.gcount()));
            return true;
        }

        for (void* a : m_archives)
        {
            HANDLE hFile = nullptr;
            if (!SFileOpenFileEx(static_cast<HANDLE>(a), name.c_str(), 0, &hFile) || !hFile) continue;
            DWORD high = 0;
            DWORD size = SFileGetFileSize(hFile, &high);
            if (size == SFILE_INVALID_SIZE) { SFileCloseFile(hFile); continue; }
            if (off >= size) { SFileCloseFile(hFile); out.clear(); return true; }
            if (len > size - off) len = size - off;
            out.resize(len);
            SFileSetFilePointer(hFile, static_cast<LONG>(off), nullptr, FILE_BEGIN);
            DWORD read = 0;
            if (len) SFileReadFile(hFile, out.data(), len, &read, nullptr);
            out.resize(read);
            SFileCloseFile(hFile);
            return true;
        }
        return false;
    }
}
