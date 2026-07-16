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
#include <limits>

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

    // Only the item subtree is indexed for by-name resolution.
    constexpr const char* kIndexPrefix = "item\\";

    struct PatchArchiveCandidate
    {
        std::string relPath;
        uint64_t rank = 0;
        bool locale = false;
    };

    bool PatchTokenRank(const std::string& tokenRaw, uint64_t& rank)
    {
        if (tokenRaw.empty()) return false;

        std::string token = ToLower(tokenRaw);
        bool allDigits = true;
        bool allLetters = true;
        for (char c : token)
        {
            allDigits = allDigits && (c >= '0' && c <= '9');
            allLetters = allLetters && (c >= 'a' && c <= 'z');
        }

        if (allDigits)
        {
            uint64_t value = 0;
            for (char c : token)
            {
                value = value * 10 + static_cast<uint64_t>(c - '0');
                if (value > 999999) return false;
            }
            if (value <= 3) return false;
            rank = value;
            return true;
        }

        if (allLetters)
        {
            uint64_t value = 0;
            for (char c : token)
            {
                const uint64_t next = value * 26 + static_cast<uint64_t>(c - 'a' + 1);
                if (next < value || next > (std::numeric_limits<uint64_t>::max() - 1000000ull))
                    return false;
                value = next;
            }
            rank = 1000000ull + value;
            return true;
        }

        return false;
    }

    void CollectPatchArchives(const std::string& root,
                              const std::string& relDir,
                              const std::string& prefix,
                              bool locale,
                              std::vector<PatchArchiveCandidate>& out)
    {
        const std::string absDir = root + "\\" + relDir;
        WIN32_FIND_DATAA fd{};
        HANDLE h = FindFirstFileA((absDir + "\\" + prefix + "*.mpq").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;

        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

            std::string name = fd.cFileName;
            std::string lower = ToLower(name);
            const std::string lowerPrefix = ToLower(prefix);
            if (lower.rfind(lowerPrefix, 0) != 0) continue;
            if (lower.size() <= lowerPrefix.size() + 4) continue;
            if (lower.substr(lower.size() - 4) != ".mpq") continue;

            std::string token = lower.substr(lowerPrefix.size(),
                                             lower.size() - lowerPrefix.size() - 4);
            uint64_t rank = 0;
            if (!PatchTokenRank(token, rank)) continue;

            out.push_back({ relDir + "\\" + name, rank, locale });
        } while (FindNextFileA(h, &fd));

        FindClose(h);
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

        // One pass over Data\* finds three things: the locale folder (carries locale-<loc>.MPQ), the
        // loose override folders (Data\Patch*.MPQ that are DIRECTORIES, highest priority), and any real
        // custom patch archives (Data\Patch*.MPQ that are FILES beyond the standard patch/-2/-3 set).
        m_locale.clear();
        std::vector<std::string> looseDirs;
        std::vector<std::string> extraArchives;
        {
            WIN32_FIND_DATAA fd{};
            HANDLE h = FindFirstFileA((data + "\\*").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE)
            {
                do
                {
                    std::string d = fd.cFileName;
                    if (d == "." || d == "..") continue;
                    const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    std::string dl = ToLower(d);
                    const bool looksLikePatch =
                        dl.rfind("patch", 0) == 0 && dl.size() > 4 && dl.substr(dl.size() - 4) == ".mpq";
                    if (isDir)
                    {
                        if (m_locale.empty() && FileExistsOnDisk(data + "\\" + d + "\\locale-" + d + ".MPQ"))
                            m_locale = d;
                        if (looksLikePatch) looseDirs.push_back(d);
                    }
                    else if (looksLikePatch && dl != "patch.mpq" && dl != "patch-2.mpq" && dl != "patch-3.mpq")
                    {
                        extraArchives.push_back(d);
                    }
                } while (FindNextFileA(h, &fd));
                FindClose(h);
            }
        }
        const std::string loc = m_locale;

        std::sort(looseDirs.begin(), looseDirs.end(), [](const std::string& a, const std::string& b) {
            return ToLower(a) > ToLower(b); // Patch-5 before Patch-4 ...
        });
        for (const std::string& d : looseDirs) m_looseRoots.push_back(data + "\\" + d + "\\");

        std::sort(extraArchives.begin(), extraArchives.end(), [](const std::string& a, const std::string& b) {
            return ToLower(a) > ToLower(b); // Patch-5.MPQ before Patch-4.MPQ ...
        });

        // Archive set, highest priority first (search order). Custom patches (Patch-4.MPQ and beyond)
        // outrank the standard patch-3/-2/base set, matching the client's own patch precedence.
        std::vector<std::string> candidates;
        for (const std::string& d : extraArchives) candidates.push_back("Data\\" + d);
        const std::vector<std::string> standard = {
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
        candidates.insert(candidates.end(), standard.begin(), standard.end());

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

        // By-name index over the item subtree, same priority order as the read path.
        const ULONGLONG i0 = GetTickCount64();
        for (const std::string& lr : m_looseRoots) IndexLooseRoot(lr);
        for (void* a : m_archives) IndexArchiveListfile(a);
        const ULONGLONG indexMs = GetTickCount64() - i0;

        log::Printf("mpq: locale=%s, %zu archives, %zu loose roots, mounted in %llu ms",
            m_locale.empty() ? "(none)" : m_locale.c_str(), m_archives.size(), m_looseRoots.size(),
            static_cast<unsigned long long>(mountMs));
        log::Printf("mpq: item index: %zu names in %llu ms",
            m_itemIndex.size(), static_cast<unsigned long long>(indexMs));
        for (size_t i = 0; i < m_archiveNames.size(); ++i)
            log::Printf("mpq:   [%zu] %s", i, m_archiveNames[i].c_str());
        for (const std::string& lr : m_looseRoots)
            log::Printf("mpq:   loose <- %s", lr.c_str());

        return !m_archives.empty() || !m_looseRoots.empty();
    }

    /**
     * @brief Records one mounted path in the file-name index if it belongs to the item subtree.
     * @param path  archive-internal path, backslash separators
     */
    void MpqStore::AddIndexEntry(const std::string& path)
    {
        const std::string lower = ToLower(path);
        if (lower.rfind(kIndexPrefix, 0) != 0) return;
        const size_t slash = lower.find_last_of('\\');
        if (slash == std::string::npos || slash + 1 >= lower.size()) return;

        std::vector<std::string>& paths = m_itemIndex[lower.substr(slash + 1)];
        for (const std::string& existing : paths)
            if (ToLower(existing) == lower) return;
        paths.push_back(path);
    }

    /**
     * @brief Walks the Item folder of a loose root and indexes every file found.
     * @param root  absolute loose root path, trailing slash
     */
    void MpqStore::IndexLooseRoot(const std::string& root)
    {
        std::vector<std::string> pending;
        pending.emplace_back("Item");
        while (!pending.empty())
        {
            std::string rel = std::move(pending.back());
            pending.pop_back();

            WIN32_FIND_DATAA fd{};
            HANDLE h = FindFirstFileA((root + rel + "\\*").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) continue;
            do
            {
                std::string entry = fd.cFileName;
                if (entry == "." || entry == "..") continue;
                std::string relEntry = rel + "\\" + entry;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    pending.push_back(std::move(relEntry));
                else
                    AddIndexEntry(relEntry);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }

    /**
     * @brief Reads the archive's (listfile) by exact name and indexes its item entries.
     * @param archive  StormLib archive HANDLE
     */
    void MpqStore::IndexArchiveListfile(void* archive)
    {
        HANDLE hFile = nullptr;
        if (!SFileOpenFileEx(static_cast<HANDLE>(archive), "(listfile)", 0, &hFile) || !hFile) return;

        std::string text;
        DWORD size = SFileGetFileSize(hFile, nullptr);
        if (size != SFILE_INVALID_SIZE && size)
        {
            text.resize(size);
            DWORD read = 0;
            SFileReadFile(hFile, text.data(), size, &read, nullptr);
            text.resize(read);
        }
        SFileCloseFile(hFile);

        size_t start = 0;
        while (start < text.size())
        {
            size_t end = text.find_first_of("\r\n", start);
            if (end == std::string::npos) end = text.size();
            if (end > start)
                AddIndexEntry(NormalizeName(std::string_view(text).substr(start, end - start)));
            start = end + 1;
        }
    }

    /**
     * @brief Picks the indexed path for `fileKey` closest to the requested location.
     * @param requestLower  lowercase requested path, used to rank candidates and reject itself
     * @param fileKey       lowercase file name to look up
     * @return the best candidate path, or nullptr if none differs from the request
     */
    const std::string* MpqStore::FindIndexed(const std::string& requestLower, const std::string& fileKey) const
    {
        auto it = m_itemIndex.find(fileKey);
        if (it == m_itemIndex.end()) return nullptr;

        const std::string* best = nullptr;
        size_t bestCommon = 0;
        for (const std::string& path : it->second)
        {
            const std::string lower = ToLower(path);
            if (lower == requestLower) continue;
            size_t common = 0;
            const size_t n = std::min(lower.size(), requestLower.size());
            while (common < n && lower[common] == requestLower[common]) ++common;
            if (!best || common > bestCommon)
            {
                best = &path;
                bestCommon = common;
            }
        }
        return best;
    }

    /**
     * @brief Resolves a missed item path to a mounted path carrying the same file name.
     * @param rawName  requested path whose exact location is absent
     * @return a serveable path with the same file name, or empty if none is indexed
     */
    std::string MpqStore::ResolveByFileName(std::string_view rawName) const
    {
        const std::string lower = ToLower(NormalizeName(rawName));
        if (lower.rfind(kIndexPrefix, 0) != 0) return {};
        const size_t slash = lower.find_last_of('\\');
        if (slash == std::string::npos || slash + 1 >= lower.size()) return {};

        const std::string file = lower.substr(slash + 1);
        const std::string* best = FindIndexed(lower, file);
        if (!best)
        {
            // Sibling extensions name the same content: model .m2/.mdx, texture .blp/.tga.
            const size_t dot = file.find_last_of('.');
            if (dot != std::string::npos)
            {
                const std::string ext = file.substr(dot);
                std::string swapped;
                if      (ext == ".m2")  swapped = ".mdx";
                else if (ext == ".mdx") swapped = ".m2";
                else if (ext == ".blp") swapped = ".tga";
                else if (ext == ".tga") swapped = ".blp";
                if (!swapped.empty())
                    best = FindIndexed(lower, file.substr(0, dot) + swapped);
            }
        }
        return best ? *best : std::string();
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
