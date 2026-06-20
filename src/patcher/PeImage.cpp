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

#include "patcher/PeImage.hpp"

#include <windows.h>
#include <cstring>

namespace
{
    /**
     * @brief Rounds a value up to the next multiple of an alignment.
     * @param v  value to align.
     * @param a  alignment, a power of two.
     * @return v rounded up to a multiple of a.
     */
    uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

    /**
     * @brief Returns the NT headers within a PE byte buffer.
     * @param b  PE file contents.
     * @return Pointer to the NT headers.
     */
    IMAGE_NT_HEADERS32* Nt(std::vector<uint8_t>& b)
    {
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(b.data());
        return reinterpret_cast<IMAGE_NT_HEADERS32*>(b.data() + dos->e_lfanew);
    }
}

namespace wxl::patcher
{
    /**
     * @brief Returns true when the buffer is a 32-bit PE with sane headers.
     * @return True if the DOS, NT, and optional-header signatures are valid.
     */
    bool PeImage::valid() const
    {
        if (bytes_.size() < sizeof(IMAGE_DOS_HEADER)) return false;
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes_.data());
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        if (static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > bytes_.size()) return false;
        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes_.data() + dos->e_lfanew);
        return nt->Signature == IMAGE_NT_SIGNATURE
            && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    }

    /**
     * @brief Returns the image's preferred load base address.
     * @return The optional header's ImageBase.
     */
    uint32_t PeImage::imageBase() const
    {
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes_.data());
        auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes_.data() + dos->e_lfanew);
        return nt->OptionalHeader.ImageBase;
    }

    /**
     * @brief Maps a virtual address to a file offset.
     * @param va  virtual address to resolve.
     * @return File offset, or 0 if the address is not backed by a section.
     */
    uint32_t PeImage::VaToOffset(uint32_t va) const
    {
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes_.data());
        auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes_.data() + dos->e_lfanew);
        const uint32_t rva = va - nt->OptionalHeader.ImageBase;
        auto* sec = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            uint32_t span = sec[i].Misc.VirtualSize > sec[i].SizeOfRawData
                          ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
            if (rva >= sec[i].VirtualAddress && rva < sec[i].VirtualAddress + span)
                return rva - sec[i].VirtualAddress + sec[i].PointerToRawData;
        }
        return 0;
    }

    /**
     * @brief Overwrites bytes at a virtual address.
     * @param va   target virtual address.
     * @param src  source bytes to copy.
     * @param len  number of bytes to write.
     * @return True on success, false if the address is not mapped to file bytes.
     */
    bool PeImage::WriteVa(uint32_t va, const void* src, uint32_t len)
    {
        uint32_t off = VaToOffset(va);
        if (off == 0 || static_cast<size_t>(off) + len > bytes_.size()) return false;
        std::memcpy(bytes_.data() + off, src, len);
        return true;
    }

    /**
     * @brief Reports whether a section with the given name exists.
     * @param name  section name, compared up to 8 chars.
     * @return True if a matching section is present.
     */
    bool PeImage::HasSection(const char* name) const
    {
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes_.data());
        auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes_.data() + dos->e_lfanew);
        auto* sec = IMAGE_FIRST_SECTION(nt);
        const size_t n = strnlen(name, 8);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
            if (memcmp(sec[i].Name, name, n) == 0 && (n == 8 || sec[i].Name[n] == 0))
                return true;
        return false;
    }

    /**
     * @brief Sets the LARGE_ADDRESS_AWARE flag, granting the 32-bit image the full 4 GB address space.
     * @return True on success.
     */
    bool PeImage::SetLargeAddressAware()
    {
        Nt(bytes_)->FileHeader.Characteristics |= IMAGE_FILE_LARGE_ADDRESS_AWARE;
        return true;
    }

    /**
     * @brief Appends an import of dll!func through a new section tagged tagSection.
     * @param dll         imported module name.
     * @param func        imported function name.
     * @param tagSection  name of the section that carries the new import.
     * @return True on success; a no-op returning true if tagSection already exists.
     */
    bool PeImage::AddImport(const char* dll, const char* func, const char* tagSection)
    {
        if (HasSection(tagSection)) return true; // already present

        IMAGE_NT_HEADERS32* nt = Nt(bytes_);
        IMAGE_FILE_HEADER&      fh = nt->FileHeader;
        IMAGE_OPTIONAL_HEADER32& oh = nt->OptionalHeader;
        IMAGE_SECTION_HEADER*   sec = IMAGE_FIRST_SECTION(nt);

        // Count existing import descriptors (terminated by an all-zero entry).
        uint32_t impRva = oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        uint32_t impOff = VaToOffset(impRva + oh.ImageBase);
        if (impOff == 0 || impOff + sizeof(IMAGE_IMPORT_DESCRIPTOR) > bytes_.size()) return false;

        auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(bytes_.data() + impOff);
        uint32_t origCount = 0;
        while (impOff + (origCount + 1) * sizeof(IMAGE_IMPORT_DESCRIPTOR) <= bytes_.size()
               && (imp[origCount].Name != 0 || imp[origCount].FirstThunk != 0))
            ++origCount;

        // --- lay out the new section blob: descriptors + INT + IAT + import-by-name + dll name ---
        const uint32_t descBytes = (origCount + 2) * sizeof(IMAGE_IMPORT_DESCRIPTOR);
        const uint32_t offDesc = 0;
        const uint32_t offInt  = offDesc + descBytes;
        const uint32_t offIat  = offInt + 2 * sizeof(uint32_t);
        const uint32_t offIbn  = offIat + 2 * sizeof(uint32_t);
        const uint32_t ibnLen  = AlignUp(2 + static_cast<uint32_t>(strlen(func)) + 1, 2);
        const uint32_t offDll  = offIbn + ibnLen;
        const uint32_t blobSize = offDll + static_cast<uint32_t>(strlen(dll)) + 1;

        std::vector<uint8_t> blob(blobSize, 0);

        IMAGE_SECTION_HEADER& last = sec[fh.NumberOfSections - 1];
        const uint32_t secRva = AlignUp(last.VirtualAddress + last.Misc.VirtualSize, oh.SectionAlignment);
        const uint32_t secRaw = AlignUp(static_cast<uint32_t>(bytes_.size()), oh.FileAlignment);

        auto* newDesc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(blob.data() + offDesc);
        memcpy(newDesc, imp, origCount * sizeof(IMAGE_IMPORT_DESCRIPTOR));
        newDesc[origCount].OriginalFirstThunk = secRva + offInt;
        newDesc[origCount].TimeDateStamp      = 0;
        newDesc[origCount].ForwarderChain     = 0;
        newDesc[origCount].Name               = secRva + offDll;
        newDesc[origCount].FirstThunk         = secRva + offIat;

        *reinterpret_cast<uint32_t*>(blob.data() + offInt) = secRva + offIbn;
        *reinterpret_cast<uint32_t*>(blob.data() + offIat) = secRva + offIbn;
        strcpy_s(reinterpret_cast<char*>(blob.data() + offIbn + 2), blobSize - (offIbn + 2), func);
        strcpy_s(reinterpret_cast<char*>(blob.data() + offDll), blobSize - offDll, dll);

        // New section header (must fit in the existing header padding).
        IMAGE_SECTION_HEADER& add = sec[fh.NumberOfSections];
        if (reinterpret_cast<uint8_t*>(&add + 1) - bytes_.data() > static_cast<ptrdiff_t>(oh.SizeOfHeaders))
            return false;
        memset(&add, 0, sizeof(add));
        memcpy(add.Name, tagSection, strnlen(tagSection, 8));
        add.Misc.VirtualSize = blobSize;
        add.VirtualAddress   = secRva;
        add.SizeOfRawData    = AlignUp(blobSize, oh.FileAlignment);
        add.PointerToRawData = secRaw;
        add.Characteristics  = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;

        oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = secRva + offDesc;
        oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size           = descBytes;
        oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].VirtualAddress = 0;
        oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].Size           = 0;
        oh.SizeOfImage = AlignUp(secRva + blobSize, oh.SectionAlignment);
        fh.NumberOfSections += 1;

        // Resizing the buffer reallocates and invalidates every pointer above; capture first.
        const uint32_t addRawSize = add.SizeOfRawData;
        bytes_.resize(secRaw, 0);
        bytes_.insert(bytes_.end(), blob.begin(), blob.end());
        bytes_.resize(secRaw + addRawSize, 0);
        return true;
    }
}
