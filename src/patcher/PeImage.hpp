// PeImage: in-memory 32-bit PE buffer with byte and structural edit operations.
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
#include <vector>

/**
 * @brief Wraps a loaded PE byte buffer and exposes byte-level and structural edit operations.
 *
 * Header pointers are recomputed on each call because AddImport can grow the buffer.
 */
namespace wxl::patcher
{
    class PeImage
    {
    public:
        /**
         * @brief Binds the toolbox to an existing PE byte buffer.
         * @param bytes  PE file contents edited in place.
         */
        explicit PeImage(std::vector<uint8_t>& bytes) : bytes_(bytes) {}

        /** @brief Returns true when the buffer is a 32-bit PE with sane headers. */
        bool     valid() const;

        /** @brief Returns the image's preferred load base address. */
        uint32_t imageBase() const;

        /**
         * @brief Maps a virtual address to a file offset.
         * @param va  virtual address to resolve.
         * @return File offset, or 0 if the address is not backed by a section.
         */
        uint32_t VaToOffset(uint32_t va) const;

        /**
         * @brief Overwrites bytes at a virtual address.
         * @param va   target virtual address.
         * @param src  source bytes to copy.
         * @param len  number of bytes to write.
         * @return True on success, false if the address is not mapped to file bytes.
         */
        bool WriteVa(uint32_t va, const void* src, uint32_t len);

        /**
         * @brief Reports whether a section with the given name exists.
         * @param name  section name, compared up to 8 chars.
         * @return True if a matching section is present.
         */
        bool HasSection(const char* name) const;

        /**
         * @brief Sets the LARGE_ADDRESS_AWARE flag, granting the 32-bit image the full 4 GB address space.
         * @return True on success.
         */
        bool SetLargeAddressAware();

        /**
         * @brief Appends an import of dll!func through a new section tagged tagSection.
         * @param dll         imported module name.
         * @param func        imported function name.
         * @param tagSection  name of the section that carries the new import.
         * @return True on success; a no-op returning true if tagSection already exists.
         */
        bool AddImport(const char* dll, const char* func, const char* tagSection);

        /** @brief Returns the underlying PE byte buffer. */
        std::vector<uint8_t>& bytes() { return bytes_; }

    private:
        std::vector<uint8_t>& bytes_;
    };
}
