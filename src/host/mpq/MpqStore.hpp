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

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Asset-agnostic archive I/O. Serves raw bytes; whether a file needs reshaping is the asset handlers'
// decision and the host stays format-blind. One instance per worker, since StormLib handles are single-thread.
namespace wxl::host::mpq
{
    /** @brief Mounts the client archive set and serves raw file bytes from it. */
    class MpqStore
    {
    public:
        /**
         * @brief Mounts the client archive set: locale and base archives plus loose Patch*.MPQ override folders.
         * @param dataDir  client data root
         * @return true if at least one archive or loose root mounted
         */
        bool Mount(std::string_view dataDir);

        /**
         * @brief Reports whether `name` exists in any mounted archive or loose root.
         * @param name  file name to check
         * @return true if the file is present
         */
        bool Exists(std::string_view name) const;

        /**
         * @brief Reads the whole file into `out`.
         * @param name  file name to read
         * @param out   receives the file bytes
         * @return false if the file is absent
         */
        bool ReadAll(std::string_view name, std::vector<uint8_t>& out) const;

        /**
         * @brief Reads a byte range from the file.
         * @param name  file name to read
         * @param off   start offset
         * @param len   maximum number of bytes to read
         * @param out   receives the bytes read (clamped to file end)
         * @return false if the file is absent
         */
        bool ReadRange(std::string_view name, uint32_t off, uint32_t len, std::vector<uint8_t>& out) const;

        /** @brief Closes all open archive handles. */
        ~MpqStore();

    private:
        // Highest priority first (search order). StormLib handles mutate on read, so mutable.
        mutable std::vector<void*> m_archives;     // StormLib HANDLEs
        std::vector<std::string>   m_archiveNames; // parallel to m_archives, for logging
        std::vector<std::string>   m_looseRoots;   // absolute folder paths, trailing slash
        std::string                m_locale;       // detected locale folder name
    };
}
