// MpqStore mount and by-name indexing: discovers the archive set and builds the item index at startup.
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
#include "mpq/MpqPath.hpp"

#include "common/Log.hpp"

#include <StormLib.h>

#include <windows.h>
#include <algorithm>
#include <limits>

using wxl::host::mpq::detail::FileExistsOnDisk;
using wxl::host::mpq::detail::NormalizeName;
using wxl::host::mpq::detail::ToLower;
using wxl::host::mpq::detail::kIndexPrefix;

namespace
{
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

        // Mount can be retried after the client path becomes available. Tear down the previous snapshot so
        // archive handles, search priority, locks, and the by-name index all describe the same mount.
        for (void* archive : m_archives)
            if (archive) SFileCloseArchive(static_cast<HANDLE>(archive));
        m_archives.clear();
        m_archiveNames.clear();
        m_archiveIsExtra.clear();
        m_archiveLocks.clear();
        m_looseRoots.clear();
        m_itemIndex.clear();

        // One pass over Data\* finds the locale folder (carries locale-<loc>.MPQ) and loose override
        // folders (Data\Patch*.MPQ that are DIRECTORIES, highest priority). Real custom archives are
        // collected separately from both Data and Data\<locale>, then ranked together.
        m_locale.clear();
        std::vector<std::string> looseDirs;
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
                } while (FindNextFileA(h, &fd));
                FindClose(h);
            }
        }
        const std::string loc = m_locale;

        std::sort(looseDirs.begin(), looseDirs.end(), [](const std::string& a, const std::string& b) {
            return ToLower(a) > ToLower(b); // Patch-5 before Patch-4 ...
        });
        for (const std::string& d : looseDirs) m_looseRoots.push_back(data + "\\" + d + "\\");

        std::vector<PatchArchiveCandidate> extraArchives;
        CollectPatchArchives(root, "Data", "patch-", false, extraArchives);
        if (!loc.empty())
            CollectPatchArchives(root, "Data\\" + loc, "patch-" + loc + "-", true, extraArchives);
        std::sort(extraArchives.begin(), extraArchives.end(), [](const PatchArchiveCandidate& a,
                                                                 const PatchArchiveCandidate& b) {
            if (a.rank != b.rank) return a.rank > b.rank;
            if (a.locale != b.locale) return a.locale; // locale wins an equal patch tier
            return ToLower(a.relPath) > ToLower(b.relPath);
        });

        // Archive set, highest priority first (search order). Include both custom base patches and custom
        // locale patches; omitting the latter makes the host see a different asset set than Wow.exe. Keep
        // the extra/standard split so Locate can skip stock files that the client reads natively.
        std::vector<std::pair<std::string, bool>> candidates; // (relative path, is extra)
        for (const PatchArchiveCandidate& archive : extraArchives)
            candidates.push_back({ archive.relPath, true });
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
        for (const std::string& rel : standard) candidates.push_back({ rel, false });

        // Resolve by exact name only (never enumerate); skip the internal (listfile) and (attributes).
        const DWORD openFlags = MPQ_OPEN_READ_ONLY | MPQ_OPEN_NO_LISTFILE | MPQ_OPEN_NO_ATTRIBUTES;

        const ULONGLONG t0 = GetTickCount64();
        for (const auto& [rel, isExtra] : candidates)
        {
            std::string full = root + "\\" + rel;
            if (!FileExistsOnDisk(full)) continue;
            HANDLE hMpq = nullptr;
            if (SFileOpenArchive(full.c_str(), 0, openFlags, &hMpq) && hMpq)
            {
                m_archives.push_back(hMpq);
                m_archiveNames.push_back(rel);
                m_archiveIsExtra.push_back(isExtra);
                m_archiveLocks.push_back(std::make_unique<std::mutex>());
            }
            else
            {
                WLOG_INFO("mpq: open failed (%lu) %s", GetLastError(), rel.c_str());
            }
        }
        const ULONGLONG mountMs = GetTickCount64() - t0;

        // By-name index over the item subtree, same priority order as the read path.
        const ULONGLONG i0 = GetTickCount64();
        for (const std::string& lr : m_looseRoots) IndexLooseRoot(lr);
        for (void* a : m_archives) IndexArchiveListfile(a);
        const ULONGLONG indexMs = GetTickCount64() - i0;

        WLOG_INFO("mpq: locale=%s, %zu archives, %zu loose roots, mounted in %llu ms",
            m_locale.empty() ? "(none)" : m_locale.c_str(), m_archives.size(), m_looseRoots.size(),
            static_cast<unsigned long long>(mountMs));
        WLOG_INFO("mpq: item index: %zu names in %llu ms",
            m_itemIndex.size(), static_cast<unsigned long long>(indexMs));
        for (size_t i = 0; i < m_archiveNames.size(); ++i)
            WLOG_INFO("mpq:   [%zu] %s", i, m_archiveNames[i].c_str());
        for (const std::string& lr : m_looseRoots)
            WLOG_INFO("mpq:   loose <- %s", lr.c_str());

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
}
