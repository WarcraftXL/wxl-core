// MpqStore read path: locates and serves raw file bytes from the mounted archive set and loose folders.
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

#include <StormLib.h>

#include <windows.h>
#include <algorithm>
#include <fstream>

using wxl::host::mpq::detail::FileExistsOnDisk;
using wxl::host::mpq::detail::NormalizeName;
using wxl::host::mpq::detail::ToLower;
using wxl::host::mpq::detail::kIndexPrefix;

namespace wxl::host::mpq
{
    /** @brief Closes all open archive handles. */
    MpqStore::~MpqStore()
    {
        for (void* h : m_archives) if (h) SFileCloseArchive(static_cast<HANDLE>(h));
        m_archives.clear();
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
        for (size_t i = 0; i < m_archives.size(); ++i)
        {
            std::lock_guard<std::mutex> lock(*m_archiveLocks[i]);
            if (SFileHasFile(static_cast<HANDLE>(m_archives[i]), name.c_str())) return true;
        }
        return false;
    }

    Source MpqStore::LocateAndRead(std::string_view rawName, bool readStandard,
                                   std::vector<uint8_t>& out) const
    {
        const std::string name = NormalizeName(rawName);

        // Loose override folders win over the archives; the open doubles as the presence test.
        for (const std::string& lr : m_looseRoots)
        {
            std::ifstream f(lr + name, std::ios::binary | std::ios::ate);
            if (!f) continue;
            std::streamoff size = f.tellg();
            f.seekg(0);
            out.resize(static_cast<size_t>(size));
            if (size) f.read(reinterpret_cast<char*>(out.data()), size);
            return Source::Loose;
        }

        for (size_t i = 0; i < m_archives.size(); ++i)
        {
            std::lock_guard<std::mutex> lock(*m_archiveLocks[i]);
            HANDLE hFile = nullptr;
            if (!SFileOpenFileEx(static_cast<HANDLE>(m_archives[i]), name.c_str(), 0, &hFile) || !hFile) continue;
            const Source source = m_archiveIsExtra[i] ? Source::Extra : Source::Standard;
            if (source == Source::Standard && !readStandard)
            {
                SFileCloseFile(hFile);
                return source; // caller answers native-skip without paying the read
            }
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
            return source;
        }
        return Source::None;
    }

    Source MpqStore::Locate(std::string_view rawName) const
    {
        const std::string name = NormalizeName(rawName);
        for (const std::string& lr : m_looseRoots)
            if (FileExistsOnDisk(lr + name)) return Source::Loose;
        for (size_t i = 0; i < m_archives.size(); ++i)
        {
            std::lock_guard<std::mutex> lock(*m_archiveLocks[i]);
            if (SFileHasFile(static_cast<HANDLE>(m_archives[i]), name.c_str()))
                return m_archiveIsExtra[i] ? Source::Extra : Source::Standard;
        }
        return Source::None;
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

        for (size_t i = 0; i < m_archives.size(); ++i)
        {
            std::lock_guard<std::mutex> lock(*m_archiveLocks[i]);
            HANDLE hFile = nullptr;
            if (!SFileOpenFileEx(static_cast<HANDLE>(m_archives[i]), name.c_str(), 0, &hFile) || !hFile) continue;
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

        for (size_t i = 0; i < m_archives.size(); ++i)
        {
            std::lock_guard<std::mutex> lock(*m_archiveLocks[i]);
            HANDLE hFile = nullptr;
            if (!SFileOpenFileEx(static_cast<HANDLE>(m_archives[i]), name.c_str(), 0, &hFile) || !hFile) continue;
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
