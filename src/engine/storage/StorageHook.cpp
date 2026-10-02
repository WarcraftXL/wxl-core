// Storage I/O hook: client-side file providers and transforms, layered over the client's own
// (now fully native) archive reads.
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

#include "engine/storage/StorageHook.hpp"

#include "engine/assets/shared/common/Text.hpp"
#include "common/Config.hpp"
#include "engine/hook/Hook.hpp"
#include "runtime/HookPoints.hpp"
#include "common/Log.hpp"
#include "common/Mem.hpp"
#include "wxl/offsets/engine/Io.hpp"

#include <windows.h>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace io = wxl::offsets::engine::io;

namespace
{
    // Marks a synthetic handle at +0x00 (a native handle holds a small kind there).
    constexpr uint32_t kHandleMagic = 0x464C5857; // 'WXLF'
    constexpr size_t   kMaxArchiveName = 512;     // upper bound when copying a name out of the native boundary

#pragma pack(push, 1)
    /**
     * @brief Synthetic file handle matching the native 0x30-byte file layout.
     *
     * The +0x14/+0x18/+0x1c fields match the native size/buffer/position fields the engine may read
     * directly. Kept at the native size and layout even though every field past +0x1c is now unused
     * -- other client code may still assume a file handle is 0x30 bytes.
     */
    struct SyntheticFile
    {
        uint32_t magic;        // +0x00  kHandleMagic
        uint32_t reserved04;   // +0x04
        uint32_t reserved08;   // +0x08
        char*    shortName;    // +0x0c
        char*    fullName;     // +0x10
        uint32_t size;         // +0x14
        uint8_t* buffer;       // +0x18  whole-file bytes, always malloc'd
        uint32_t position;     // +0x1c
        uint32_t reserved20;   // +0x20
        uint32_t reserved24;   // +0x24
        void*    reserved28;   // +0x28
        void*    reserved2c;   // +0x2c
    };
#pragma pack(pop)
    static_assert(sizeof(SyntheticFile) == 0x30, "SyntheticFile must match the native 0x30-byte file layout");

    /**
     * @brief Reports whether a handle is a synthetic handle.
     * @param h  handle to test.
     * @return true when the handle carries kHandleMagic.
     */
    bool IsOurs(void* h)
    {
        return h && *reinterpret_cast<uint32_t*>(h) == kHandleMagic;
    }

    io::Storage_FileOpenFn  g_origOpen  = nullptr;
    io::Storage_FileSizeFn  g_origSize  = nullptr;
    io::Storage_FileReadFn  g_origRead  = nullptr;
    io::Storage_FileSeekFn  g_origSeek  = nullptr;
    io::Storage_FileCloseFn g_origClose = nullptr;
    io::InitializeWowConfigFn g_origInitializeWowConfig = nullptr;

    uint32_t g_served = 0; // opens claimed by a client provider or reshaped by a client transform
    uint32_t g_opens  = 0; // intercept attempts

    // Providers/transforms/filters are registered by module installers while loader threads already
    // run the open detours; every access to these registries goes through this mutex, and iteration
    // works on a snapshot so no module callback runs under the lock.
    std::mutex& RegistryMutex()
    {
        static std::mutex m;
        return m;
    }

    std::vector<wxl::runtime::storage::ClientProvideFn>& ClientProviders()
    {
        static std::vector<wxl::runtime::storage::ClientProvideFn> v;
        return v;
    }

    /** @brief Returns a snapshot of the client providers safe to iterate without the lock. */
    std::vector<wxl::runtime::storage::ClientProvideFn> ClientProvidersSnapshot()
    {
        std::lock_guard<std::mutex> lock(RegistryMutex());
        return ClientProviders();
    }

    std::vector<wxl::runtime::storage::ClientRedirectFn>& ClientRedirects()
    {
        static std::vector<wxl::runtime::storage::ClientRedirectFn> v;
        return v;
    }

    /** @brief Returns a snapshot of the client redirects safe to iterate without the lock. */
    std::vector<wxl::runtime::storage::ClientRedirectFn> ClientRedirectsSnapshot()
    {
        std::lock_guard<std::mutex> lock(RegistryMutex());
        return ClientRedirects();
    }

    struct TransformEntry
    {
        std::string suffix; // lowercase, e.g. ".blp"
        wxl::runtime::storage::ClientTransformFn fn;
    };

    std::vector<TransformEntry>& ClientTransforms()
    {
        static std::vector<TransformEntry> v;
        return v;
    }

    /** @brief Returns a snapshot of the client transforms safe to iterate without the lock. */
    std::vector<TransformEntry> ClientTransformsSnapshot()
    {
        std::lock_guard<std::mutex> lock(RegistryMutex());
        return ClientTransforms();
    }

    std::vector<wxl::runtime::storage::ServeFilterFn>& ServeFilters()
    {
        static std::vector<wxl::runtime::storage::ServeFilterFn> filters;
        return filters;
    }

    /** @brief Returns a snapshot of the serve filters safe to iterate without the lock. */
    std::vector<wxl::runtime::storage::ServeFilterFn> ServeFiltersSnapshot()
    {
        std::lock_guard<std::mutex> lock(RegistryMutex());
        return ServeFilters();
    }

    using wxl::modern::assets::common::text::EndsWithCI;

    /** @brief Returns the first registered transform whose suffix matches name, or null. */
    wxl::runtime::storage::ClientTransformFn FindClientTransform(std::string_view name)
    {
        for (const TransformEntry& e : ClientTransformsSnapshot())
            if (EndsWithCI(name, e.suffix))
                return e.fn;
        return nullptr;
    }

    // Reads the same keys as assets::common::env::VerboseAssetLogs() through WarcraftXL.cfg as well,
    // plus WXL_CLIENT_STORAGE_LOGS; an explicit falsy value on an earlier key wins over the later ones.
    bool VerboseStorageLogs()
    {
        static const bool enabled =
            wxl::config::Env("WXL_VERBOSE_ASSET_LOGS",
                wxl::config::Env("WXL_ASSET_LOGS",
                    wxl::config::Env("WXL_CLIENT_STORAGE_LOGS", false)));
        return enabled;
    }

    /**
     * @brief Reports whether a name is worth offering to client providers/transforms at all.
     *
     * Skips .pub/.url, which are existence probes rather than archive content. Skips .tex, the per-map
     * texture catalog nothing here reads. Skips audio (.wav/.mp3/.ogg): world sound loads read through
     * the client's async path, which a synthetic handle never completes.
     * @param name  file name to test.
     * @return true when the name is worth checking.
     */
    bool ShouldIntercept(std::string_view name)
    {
        if (EndsWithCI(name, ".pub") || EndsWithCI(name, ".url")) return false;
        if (EndsWithCI(name, ".tex")) return false;
        if (EndsWithCI(name, ".wav") || EndsWithCI(name, ".mp3") || EndsWithCI(name, ".ogg")) return false;
        return true;
    }

    /**
     * @brief Copies a plausible archive path out of the native call boundary.
     *
     * The native open surface occasionally receives non-path sentinels or stale small integers whose bytes
     * look like a string (e.g. a lone 0x01). Validates content only -- a wild pointer is assumed not to occur here.
     * @param name  native name pointer.
     * @param out   receives a bounded, validated copy.
     * @return true when the bytes look like a normal archive path.
     */
    bool CopyArchiveName(const char* name, std::string& out)
    {
        if (!name) return false;
        out.clear();
        for (size_t i = 0; i < kMaxArchiveName; ++i)
        {
            unsigned char c = static_cast<unsigned char>(name[i]);
            if (c == '\0') return !out.empty();
            if (c < 0x20 || c >= 0x7f) return false; // control/extended byte: not a normal archive path
            out.push_back(static_cast<char>(c));
        }
        return false; // unterminated within the bound: treat as bogus
    }

    /**
     * @brief Duplicates a null-terminated string into a malloc'd buffer.
     * @param s  source string.
     * @return the duplicated string, or null on allocation failure.
     */
    char* DupName(const char* s)
    {
        size_t n = strlen(s) + 1;
        char* p = static_cast<char*>(malloc(n));
        if (p) memcpy(p, s, n);
        return p;
    }

    /**
     * @brief Builds a buffered synthetic handle owning a copy of bytes.
     * @param name  file name the handle answers to (logging + module filters).
     * @param data  bytes to copy in; may be null when size is 0.
     * @param size  byte count.
     * @return the new handle, or null on allocation failure.
     */
    SyntheticFile* BuildBufferedHandle(const std::string& name, const uint8_t* data, uint32_t size)
    {
        auto* f = static_cast<SyntheticFile*>(calloc(1, sizeof(SyntheticFile)));
        if (!f) return nullptr;
        f->magic = kHandleMagic;
        f->size = size;
        f->buffer = static_cast<uint8_t*>(malloc(size ? size : 1));
        if (!f->buffer) { free(f); return nullptr; }
        if (size && data) memcpy(f->buffer, data, size);
        f->fullName = DupName(name.c_str());
        f->shortName = f->fullName;

        for (wxl::runtime::storage::ServeFilterFn filter : ServeFiltersSnapshot())
        {
            const uint32_t served = filter(name.c_str(), f->buffer, f->size);
            if (served < f->size) f->size = served;
        }
        return f;
    }

    /**
     * @brief Tries every registered client provider for name, building a synthetic handle on a claim.
     * @param name  validated archive name.
     * @param out   receives the synthetic handle on a claim.
     * @return true on a claim, false to let the native open run.
     */
    bool TryClientProvider(const std::string& name, void** out)
    {
        std::vector<uint8_t> provided;
        for (auto fn : ClientProvidersSnapshot())
        {
            if (!fn(name.c_str(), provided)) continue;
            SyntheticFile* f = BuildBufferedHandle(name, provided.data(), static_cast<uint32_t>(provided.size()));
            if (!f) break;
            if (out) *out = f;
            ++g_served;
            if (VerboseStorageLogs() && g_served < 60)
                WLOG_INFO("Storage: '%s' supplied by client provider (%u B)", name.c_str(), f->size);
            return true;
        }
        return false;
    }

    /**
     * @brief Attempts a client-provider claim before the native open runs.
     * @param archive  archive object; specific-archive opens (non-null) stay native.
     * @param name     file name.
     * @param out      receives the resulting handle on a claim.
     * @return true on a claim, false to let the native open run.
     */
    bool TryServe(void* archive, const char* name, void** out)
    {
        std::string safeName;
        if (!CopyArchiveName(name, safeName) || !ShouldIntercept(safeName)) return false;

        // Specific-archive opens usually name files the client wants from one concrete MPQ; a
        // provider deals in served *names*, not archive membership, so it stays scoped to the
        // generic (archive == nullptr) search-path opens.
        if (archive != nullptr) return false;

        if ((++g_opens % 2000) == 0)
            WLOG_INFO("Storage stats: opens=%u served=%u", g_opens, g_served);

        return TryClientProvider(safeName, out);
    }

    /**
     * @brief Runs a native open's bytes through any matching client transform, swapping the handle.
     *
     * Reads the whole file through the native handle it was just given, offers it to the first
     * matching transform, and on a claim replaces *out with a buffered handle over the reshaped
     * bytes (closing the native one). On a decline or any read failure the native handle is left for
     * the real caller, rewound to position 0 first -- this function's own speculative read must never
     * leave that handle sitting at EOF for whoever actually asked for it.
     * @param name    validated archive name.
     * @param handle  the native handle *out already holds.
     * @param out     receives the replacement handle on a claim.
     */
    void TryNativeTransform(const std::string& name, void* handle, void** out)
    {
        wxl::runtime::storage::ClientTransformFn transform = FindClientTransform(name);
        if (!transform) return;

        uint32_t sizeHigh = 0;
        const uint32_t size = g_origSize(handle, &sizeHigh);
        if (sizeHigh != 0 || size == 0) return; // >4GB or empty: not a texture, leave native

        std::vector<uint8_t> raw(size);
        uint32_t got = 0;
        const bool readOk = g_origRead(handle, raw.data(), size, &got, nullptr, 0) && got == size;
        if (readOk)
        {
            std::vector<uint8_t> reshaped;
            if (transform(name.c_str(), raw, reshaped))
            {
                SyntheticFile* f = BuildBufferedHandle(name, reshaped.data(), static_cast<uint32_t>(reshaped.size()));
                if (f)
                {
                    g_origClose(handle);
                    if (out) *out = f;
                    ++g_served;
                    if (VerboseStorageLogs() && g_served < 60)
                        WLOG_INFO("Storage: '%s' reshaped by client transform (%u -> %u B)",
                                  name.c_str(), size, f->size);
                    return;
                }
            }
        }
        // Declined, or the speculative read/reshape failed: the native handle stays the real answer,
        // so undo the read this function did on it before handing it back. distHigh is a real
        // variable, not null: unlike this file's own SeekDetour, the native Seek's null-tolerance
        // for that out-param isn't known.
        uint32_t distHigh = 0;
        g_origSeek(handle, 0, &distHigh, 0);
    }

    /**
     * @brief Detours the per-file content open entry point.
     * @param archive  archive object.
     * @param name     file name.
     * @param flags    native open flags.
     * @param out      receives the resulting handle.
     * @return 1 on a claim, otherwise the native open result.
     */
    /// Lets a redirect answer this request with a different file. Runs before everything else so the
    /// rest of the path, and every consumer past it, only ever sees the name that won.
    const char* Redirect(const char* name, std::string& scratch)
    {
        if (!name) return name;
        for (wxl::runtime::storage::ClientRedirectFn fn : ClientRedirectsSnapshot())
        {
            scratch.clear();
            if (fn(name, scratch) && !scratch.empty()) return scratch.c_str();
        }
        return name;
    }

    int __stdcall OpenDetour(void* archive, const char* name, uint32_t flags, void** out)
    {
        std::string redirected;
        name = Redirect(name, redirected);

        if (TryServe(archive, name, out)) return 1;

        const int result = g_origOpen(archive, name, flags, out);
        if (result != 0 && out && *out)
        {
            std::string safeName;
            if (CopyArchiveName(name, safeName) && ShouldIntercept(safeName))
                TryNativeTransform(safeName, *out, out);
        }
        return result;
    }

    /**
     * @brief Detours file size, returning the buffered size for synthetic handles.
     * @param handle    file handle.
     * @param sizeHigh  receives the high 32 bits (always 0 for synthetic handles).
     * @return the low 32 bits of the file size.
     */
    uint32_t __stdcall SizeDetour(void* handle, uint32_t* sizeHigh)
    {
        if (IsOurs(handle))
        {
            if (sizeHigh) *sizeHigh = 0;
            return reinterpret_cast<SyntheticFile*>(handle)->size;
        }
        return g_origSize(handle, sizeHigh);
    }

    /**
     * @brief Detours file read, copying out of the buffer for synthetic handles.
     * @param handle  file handle.
     * @param dst     destination buffer.
     * @param len     requested byte count.
     * @param read    receives the bytes copied.
     * @param ovl     native overlapped parameter.
     * @param unk     native read parameter.
     * @return 1 when the full request was satisfied, otherwise 0.
     */
    int __stdcall ReadDetour(void* handle, void* dst, uint32_t len, uint32_t* read, void* ovl, uint32_t unk)
    {
        if (IsOurs(handle))
        {
            auto* f = reinterpret_cast<SyntheticFile*>(handle);
            uint32_t avail = (f->position < f->size) ? (f->size - f->position) : 0;
            uint32_t want = (len < avail) ? len : avail;
            if (want) memcpy(dst, f->buffer + f->position, want);
            f->position += want;
            if (read) *read = want;
            return (want == len) ? 1 : 0; // nonzero only when the full request was satisfied
        }
        return g_origRead(handle, dst, len, read, ovl, unk);
    }

    /**
     * @brief Detours file seek, clamping the position within the buffer for synthetic handles.
     * @param handle    file handle.
     * @param distLow   signed seek distance.
     * @param distHigh  receives the high 32 bits of the resulting position (always 0).
     * @param method    seek origin: 0=begin, 1=current, 2=end.
     * @return the resulting position.
     */
    uint32_t __stdcall SeekDetour(void* handle, int32_t distLow, uint32_t* distHigh, uint32_t method)
    {
        if (IsOurs(handle))
        {
            auto* f = reinterpret_cast<SyntheticFile*>(handle);
            int64_t base = (method == 1) ? f->position : (method == 2) ? f->size : 0; // 0=BEGIN,1=CURRENT,2=END
            int64_t pos = base + distLow;
            if (pos < 0) pos = 0;
            if (pos > f->size) pos = f->size;
            f->position = static_cast<uint32_t>(pos);
            if (distHigh) *distHigh = 0;
            return f->position;
        }
        return g_origSeek(handle, distLow, distHigh, method);
    }

    /**
     * @brief Detours file close, releasing the buffer for synthetic handles.
     * @param handle  file handle.
     * @return 1 for synthetic handles, otherwise the native close result.
     */
    int __stdcall CloseDetour(void* handle)
    {
        if (IsOurs(handle))
        {
            auto* f = reinterpret_cast<SyntheticFile*>(handle);
            free(f->buffer);
            free(f->fullName);
            free(f);
            return 1;
        }
        return g_origClose(handle);
    }

    // InitializeWowConfig's required-archive hard-fail gate (io::kRequiredArchiveGateJnz):
    // `0F 85 11 01 00 00` (jne 0x4061d4) -> `E9 12 01 00 00 90` (unconditional jmp + 1-byte NOP pad).
    // Both encodings are 6 bytes so nothing after the patch site needs to move. Turns a genuinely
    // corrupted/missing required archive into a tolerated, logged skip instead of a hard "Failed to
    // open archive" dialog + exit.
    constexpr uint8_t kRequiredGateOriginal[6] = { 0x0F, 0x85, 0x11, 0x01, 0x00, 0x00 };
    constexpr uint8_t kRequiredGatePatched[6]  = { 0xE9, 0x12, 0x01, 0x00, 0x00, 0x90 };

    /**
     * @brief Detours InitializeWowConfig: runs the original archive-mount/signature-check/config-parse
     *        sequence unmodified, then forces the expansion-content-present flag on.
     */
    void __cdecl InitializeWowConfigDetour()
    {
        g_origInitializeWowConfig();
        *reinterpret_cast<uint8_t*>(io::kWotlkContentFlag) = 2;
    }
}

namespace wxl::runtime::storage
{
    /**
     * @brief Arms the archive-mount safety nets (required-archive hard-fail gate, expansion-content
     *        flag force). Independent of Install(); every archive -- named, loose-directory, or a
     *        Patch-* the client's own patch-name scan finds -- mounts exactly as stock from here.
     *
     * Must run before the client builds its archive set (call it from the DLL entry, on the loader
     * thread, before the client's startup proceeds).
     */
    void InstallArchiveGuard()
    {
        wxl::runtime::hookpoints::Attach("Io.InitializeWowConfig",
            &InitializeWowConfigDetour, &g_origInitializeWowConfig);

        auto* gate = reinterpret_cast<void*>(io::kRequiredArchiveGateJnz);
        if (memcmp(gate, kRequiredGateOriginal, sizeof kRequiredGateOriginal) == 0)
        {
            wxl::mem::Patch(gate, kRequiredGatePatched, sizeof kRequiredGatePatched);
            WLOG_INFO("Storage: required-archive gate patched (boot no longer hard-fails on a blocked mount)");
        }
        else
        {
            WLOG_INFO("Storage: required-archive gate bytes did not match expected -- NOT patched (build mismatch?)");
        }

        wxl::hook::EnableAll();
        WLOG_INFO("Storage: archive-mount safety nets armed");
    }

    /**
     * @brief Installs the archive file-I/O detours so client providers and transforms can run.
     */
    void Install()
    {
        wxl::runtime::hookpoints::Attach("Io.FileOpen",  &OpenDetour, &g_origOpen);
        wxl::runtime::hookpoints::Attach("Io.FileSize",  &SizeDetour, &g_origSize);
        wxl::runtime::hookpoints::Attach("Io.FileRead",  &ReadDetour, &g_origRead);
        wxl::runtime::hookpoints::Attach("Io.FileSeek",  &SeekDetour, &g_origSeek);
        wxl::runtime::hookpoints::Attach("Io.FileClose", &CloseDetour, &g_origClose);
        WLOG_INFO("Storage: hooks installed");
    }

    void RegisterClientProvider(ClientProvideFn fn)
    {
        if (!fn) return;
        std::lock_guard<std::mutex> lock(RegistryMutex());
        ClientProviders().push_back(fn);
    }

    void RegisterClientRedirect(ClientRedirectFn fn)
    {
        if (!fn) return;
        std::lock_guard<std::mutex> lock(RegistryMutex());
        ClientRedirects().push_back(fn);
    }

    void RegisterClientTransform(const char* suffix, ClientTransformFn fn)
    {
        if (!fn || !suffix || !*suffix) return;
        std::string lower(suffix);
        for (char& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        std::lock_guard<std::mutex> lock(RegistryMutex());
        ClientTransforms().push_back(TransformEntry{ std::move(lower), fn });
    }

    void RegisterServeFilter(ServeFilterFn fn)
    {
        if (!fn) return;
        std::lock_guard<std::mutex> lock(RegistryMutex());
        ServeFilters().push_back(fn);
    }
}
