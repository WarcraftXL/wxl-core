// WRAITH IAT Patcher.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    constexpr char kDllName[]  = "Wraith.dll";
    constexpr char kFuncName[] = "Wraith";
    constexpr char kSection[]  = ".wraith";

    uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

    std::vector<uint8_t> ReadFile(const char* path)
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

    bool WriteFile(const char* path, const std::vector<uint8_t>& data)
    {
        FILE* f = nullptr;
        if (fopen_s(&f, path, "wb") != 0 || !f) return false;
        fwrite(data.data(), 1, data.size(), f);
        fclose(f);
        return true;
    }
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char* target = argc > 1 ? argv[1] : "Wow.exe";
    printf("[WRAITH] patcher start, target='%s'\n", target);

    std::vector<uint8_t> file = ReadFile(target);
    if (file.empty()) { printf("[WRAITH] cannot read '%s'\n", target); return 1; }

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(file.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) { printf("[WRAITH] not a PE (MZ)\n"); return 1; }
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(file.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) { printf("[WRAITH] not a PE (NT)\n"); return 1; }

    IMAGE_FILE_HEADER&     fh = nt->FileHeader;
    IMAGE_OPTIONAL_HEADER32& oh = nt->OptionalHeader;
    IMAGE_SECTION_HEADER*  sec = IMAGE_FIRST_SECTION(nt);

    for (int i = 0; i < fh.NumberOfSections; ++i)
        if (memcmp(sec[i].Name, kSection, 7) == 0)
        { printf("[WRAITH] already patched ('%s' section present)\n", kSection); return 0; }

    // Give the 32-bit client the full 4 GB address space on 64-bit Windows (the "4GB patch").
    if (!(fh.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE))
    {
        fh.Characteristics |= IMAGE_FILE_LARGE_ADDRESS_AWARE;
        printf("[WRAITH] set LARGE_ADDRESS_AWARE (4 GB)\n");
    }

    auto RvaToOffset = [&](uint32_t rva) -> uint32_t {
        for (int i = 0; i < fh.NumberOfSections; ++i)
        {
            uint32_t span = sec[i].Misc.VirtualSize > sec[i].SizeOfRawData
                          ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
            if (rva >= sec[i].VirtualAddress && rva < sec[i].VirtualAddress + span)
                return rva - sec[i].VirtualAddress + sec[i].PointerToRawData;
        }
        return 0;
    };

    // GlueXML signature unlock
    // Byte set ported from WotLK-Extensions (MIT, (c) Alyst3r).
    {
        struct BytePatch { uint32_t va; uint8_t bytes[8]; uint32_t len; };
        const BytePatch glueUnlock[] = {
            { 0x5F4DBF, { 0xEB }, 1 },
            { 0x816625, { 0xEB }, 1 },
            { 0x81663F, { 0x03 }, 1 },
            { 0x816695, { 0x03 }, 1 },
            { 0x816746, { 0xEB }, 1 },
            { 0x81675F, { 0xB8, 0x03, 0x00, 0x00, 0x00, 0xEB, 0xED }, 7 },
        };
        for (const BytePatch& bp : glueUnlock)
        {
            uint32_t off = RvaToOffset(bp.va - (uint32_t)oh.ImageBase);
            if (off == 0 || off + bp.len > file.size())
            { printf("[WRAITH] glue-unlock VA 0x%X not mapped to file\n", bp.va); return 1; }
            memcpy(file.data() + off, bp.bytes, bp.len);
        }
        printf("[WRAITH] applied GlueXML Lua/XML unlock (%zu patches)\n",
               sizeof(glueUnlock) / sizeof(glueUnlock[0]));
    }

    // Count the existing import descriptors (terminated by an all-zero entry).
    uint32_t impRva = oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    uint32_t impOff = RvaToOffset(impRva);
    if (impOff == 0 || impOff + sizeof(IMAGE_IMPORT_DESCRIPTOR) > file.size())
    { printf("[WRAITH] import dir RVA 0x%X not mapped to file\n", impRva); return 1; }

    auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(file.data() + impOff);
    uint32_t origCount = 0;
    while (impOff + (origCount + 1) * sizeof(IMAGE_IMPORT_DESCRIPTOR) <= file.size()
           && (imp[origCount].Name != 0 || imp[origCount].FirstThunk != 0))
        ++origCount;
    printf("[WRAITH] import dir RVA=0x%X off=0x%X, %u existing imports\n", impRva, impOff, origCount);

    // --- lay out the new section blob ---
    const uint32_t descBytes = (origCount + 2) * sizeof(IMAGE_IMPORT_DESCRIPTOR); // originals + ours + null
    const uint32_t offDesc = 0;
    const uint32_t offInt  = offDesc + descBytes;        // import name table (2 thunks: name, null)
    const uint32_t offIat  = offInt + 2 * sizeof(uint32_t);
    const uint32_t offIbn  = offIat + 2 * sizeof(uint32_t);
    const uint32_t ibnLen  = AlignUp(2 + (uint32_t)strlen(kFuncName) + 1, 2);
    const uint32_t offDll  = offIbn + ibnLen;
    const uint32_t blobSize = offDll + (uint32_t)strlen(kDllName) + 1;

    std::vector<uint8_t> blob(blobSize, 0);

    // New section placement.
    IMAGE_SECTION_HEADER& last = sec[fh.NumberOfSections - 1];
    const uint32_t secRva = AlignUp(last.VirtualAddress + last.Misc.VirtualSize, oh.SectionAlignment);
    const uint32_t secRaw = AlignUp((uint32_t)file.size(), oh.FileAlignment);

    // Descriptors: copy originals, append ours, then the null terminator (blob is zero-filled).
    auto* newDesc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(blob.data() + offDesc);
    memcpy(newDesc, imp, origCount * sizeof(IMAGE_IMPORT_DESCRIPTOR));
    newDesc[origCount].OriginalFirstThunk = secRva + offInt;
    newDesc[origCount].TimeDateStamp      = 0;
    newDesc[origCount].ForwarderChain     = 0;
    newDesc[origCount].Name               = secRva + offDll;
    newDesc[origCount].FirstThunk         = secRva + offIat;

    // INT + IAT both point at the import-by-name; the loader overwrites the IAT with the resolved addr.
    *reinterpret_cast<uint32_t*>(blob.data() + offInt) = secRva + offIbn;
    *reinterpret_cast<uint32_t*>(blob.data() + offIat) = secRva + offIbn;
    // import-by-name: WORD hint (0) then the function name.
    strcpy_s(reinterpret_cast<char*>(blob.data() + offIbn + 2), blobSize - (offIbn + 2), kFuncName);
    strcpy_s(reinterpret_cast<char*>(blob.data() + offDll), blobSize - offDll, kDllName);

    // New section header (must fit in the existing header padding).
    IMAGE_SECTION_HEADER& add = sec[fh.NumberOfSections];
    if (reinterpret_cast<uint8_t*>(&add + 1) - file.data() > oh.SizeOfHeaders)
    { printf("[WRAITH] no room for a new section header\n"); return 1; }
    memset(&add, 0, sizeof(add));
    memcpy(add.Name, kSection, strlen(kSection));
    add.Misc.VirtualSize = blobSize;
    add.VirtualAddress   = secRva;
    add.SizeOfRawData    = AlignUp(blobSize, oh.FileAlignment);
    add.PointerToRawData = secRaw;
    add.Characteristics  = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;

    // Repoint the import directory at our table; clear bound imports; grow the image.
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = secRva + offDesc;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size           = descBytes;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].VirtualAddress = 0;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].Size           = 0;
    oh.SizeOfImage = AlignUp(secRva + blobSize, oh.SectionAlignment);
    fh.NumberOfSections += 1;

    // Append the blob at the new raw offset (pad the gap, then the section to file alignment).
    // NOTE: resizing `file` reallocates and invalidates every pointer into it (add/oh/nt/sec), so
    // capture what we still need first.
    const uint32_t addRawSize = add.SizeOfRawData;
    file.resize(secRaw, 0);
    file.insert(file.end(), blob.begin(), blob.end());
    file.resize(secRaw + addRawSize, 0);

    // Back up the original once, then write.
    std::string backup = std::string(target) + ".orig";
    if (GetFileAttributesA(backup.c_str()) == INVALID_FILE_ATTRIBUTES)
        CopyFileA(target, backup.c_str(), TRUE);

    if (!WriteFile(target, file)) { printf("[WRAITH] cannot write '%s'\n", target); return 1; }

    printf("[WRAITH] patched '%s': +import %s!%s (%u existing imports kept, backup '%s')\n",
           target, kDllName, kFuncName, origCount, backup.c_str());
    return 0;
}
