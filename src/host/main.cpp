// WarcraftXLHost.exe (64-bit) entry point: archive owner and IPC transport that serves files to the client.
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

#include "Host.hpp"
#include "Profile.hpp"
#include "ipc/Protocol.hpp"
#include "ipc/ShmServer.hpp"
#include "mpq/MpqStore.hpp"
#include "core/Logger.hpp"

#include <flatbuffers/flexbuffers.h>

#include <windows.h>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace wxl::ipc;
using wxl::host::mpq::MpqStore;
namespace wlog = wxl::core::log; // 'log' alone collides with ::log from <cmath>
namespace hprof = wxl::host::profile;

// Console output is opt-in at runtime via --console; the file log is always on.
static bool g_console = false;
static bool g_consoleOpenLog = false;
#define HOST_CONSOLE(...) do { if (g_console) printf(__VA_ARGS__); } while (0)
#define HOST_OPEN_CONSOLE(...) do { if (g_console && g_consoleOpenLog) printf(__VA_ARGS__); } while (0)

namespace
{
    std::string g_dataDir;       // host data folder (defaults to ExeDir; --data overrides)
    DWORD       g_clientPid = 0; // client process to shadow; host exits when it closes
    std::string g_clientRoot;    // client data root (the host runs from the client Utils folder)

    // Serve store, shared by every channel worker. StormLib handles are single-thread, so raw archive
    // I/O is serialized behind g_mpqMutex; the much more expensive Transform call that follows a read
    // runs outside the lock, so worker threads still reshape different files concurrently. Mounted lazily.
    std::unique_ptr<MpqStore> g_mpq;
    std::mutex                g_mpqMutex;

    /** @brief Serialized MpqStore::ReadAll. @return false if the file is absent. */
    bool MpqReadAll(const std::string& name, std::vector<uint8_t>& out)
    {
        std::lock_guard<std::mutex> lock(g_mpqMutex);
        return g_mpq->ReadAll(name, out);
    }

    /** @brief Serialized MpqStore::Exists. @return true if the file is present. */
    bool MpqExists(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(g_mpqMutex);
        return g_mpq->Exists(name);
    }

    /** @brief Serialized MpqStore::ResolveByFileName. @return a serveable path, or empty if none is indexed. */
    std::string MpqResolveByFileName(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(g_mpqMutex);
        return g_mpq->ResolveByFileName(name);
    }

    // Large files are served zero-copy: the host copies the bytes once into a named shared section and
    // keeps it alive until the client closes the id.

    /** @brief Holds a shared section serving one file's bytes zero-copy to the client. */
    struct Blob { HANDLE section; void* view; uint32_t size; uint32_t refs; std::string name; };
    std::mutex g_handleMutex;
    std::unordered_map<uint32_t, Blob> g_blobs;
    std::unordered_map<std::string, uint32_t> g_blobByName;
    uint64_t g_blobBytes = 0; // protected by g_handleMutex
    uint64_t g_blobRefs = 0;  // protected by g_handleMutex
    uint32_t g_nextHandle = 0;

    std::string ClientRoot();

    // Transforms are pure for a given archive path. Cache transformed outputs in the 64-bit host so expensive
    // cold work like DXT component palettization and MD21 dechunk/downport is not repeated on every reopen.
    struct TransformCacheEntry
    {
        std::vector<uint8_t> bytes;
        std::list<std::string>::iterator lru;
    };

    std::mutex g_transformCacheMutex;
    std::list<std::string> g_transformCacheLru; // least recently used at the front
    std::unordered_map<std::string, TransformCacheEntry> g_transformCache;
    size_t g_transformCacheBytes = 0;
    std::mutex g_profileMutex;

    bool EnvTruthy(const char* raw)
    {
        if (!raw || !*raw) return false;
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(*raw)));
        return c != '0' && c != 'n' && c != 'f';
    }

    uint64_t EnvU64(const char* name, uint64_t fallback, uint64_t minValue, uint64_t maxValue)
    {
        const char* raw = std::getenv(name);
        if (!raw || !*raw) return fallback;

        char* end = nullptr;
        uint64_t value = std::strtoull(raw, &end, 10);
        if (end == raw) return fallback;
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    size_t TransformCacheMaxBytes()
    {
        static const size_t bytes = static_cast<size_t>(
            EnvU64("WXL_TRANSFORM_CACHE_MB", 128, 32, 4096) * 1024ull * 1024ull);
        return bytes;
    }

    size_t TransformCacheMaxEntry()
    {
        static const size_t bytes = static_cast<size_t>(
            EnvU64("WXL_TRANSFORM_CACHE_ENTRY_MB", 8, 1, 256) * 1024ull * 1024ull);
        return bytes;
    }

    bool ConsoleOpenLogRequested()
    {
        if (EnvTruthy(std::getenv("WXL_HOST_CONSOLE_OPENS"))) return true;
        const std::string root = ClientRoot();
        if (root.empty()) return false;
        return GetFileAttributesA((root + "\\WarcraftXLConsoleOpens.flag").c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    /**
     * @brief Returns the folder containing the host executable.
     * @return host executable folder, or "." if it cannot be determined
     */
    std::string ExeDir()
    {
        char p[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, p, MAX_PATH);
        std::string s(p, n);
        size_t slash = s.find_last_of("\\/");
        return (slash == std::string::npos) ? std::string(".") : s.substr(0, slash);
    }

    /**
     * @brief Returns the client data root, the parent of the host data folder.
     * @return client data root path
     */
    std::string ClientRoot()
    {
        std::string root = g_dataDir;
        size_t slash = root.find_last_of("\\/");
        if (slash != std::string::npos) root = root.substr(0, slash);
        return root;
    }

    /**
     * @brief Picks the worker-thread count from the channel count: half, floored to at least one.
     *
     * Channels bound how many requests the client can have in flight; workers bound how many run at
     * once. Using fewer workers than channels leaves headroom on the machine for the game client itself
     * rather than pinning one CPU-bound thread to every logical core during a streaming burst.
     * @param channelCount  channel count chosen by wxl::host::ipc::Create()
     * @return worker thread count, at least 1
     */
    uint32_t ComputeWorkerCount(uint32_t channelCount)
    {
        const uint32_t workers = channelCount / 2;
        return workers ? workers : 1;
    }

    /**
     * @brief Waits on the client process handle and exits the host when the client closes.
     * @param clientHandle  HANDLE to the client process, passed as the thread parameter
     * @return thread exit code (unreached)
     */
    DWORD WINAPI ClientWatcher(LPVOID clientHandle)
    {
        WaitForSingleObject(static_cast<HANDLE>(clientHandle), INFINITE);
        // The watcher terminates the process directly, bypassing main()/Close(). Preserve buffered startup
        // and the most recent completed profile window before the OS tears down the CRT.
        wlog::Flush();
        ExitProcess(0);
        return 0;
    }

    /** @brief Warms lazy FileDataID resolver tables below normal priority, off the client request thread. */
    DWORD WINAPI ResolverWarmer(LPVOID)
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        wxl::host::WarmResolvers();
        return 0;
    }

    /**
     * @brief Copies whole bytes into a named shared section and returns its nonzero id.
     *
     * Repeated opens of the same archive name share one section with a refcount. This is especially useful
     * for looping sounds and hot city textures, which otherwise create and map a fresh host blob on every
     * open even when the payload is identical.
     * @param name   file name whose bytes are being served
     * @param bytes  file bytes to place in the section
     * @param reused receives true when an existing named section was retained
     * @return the nonzero blob id, or 0 if the section or view could not be created
     */
    uint32_t StoreBlob(const std::string& name, const std::vector<uint8_t>& bytes, bool& reused)
    {
        reused = false;
        uint32_t size = static_cast<uint32_t>(bytes.size());

        std::lock_guard<std::mutex> hl(g_handleMutex);
        auto existingName = g_blobByName.find(name);
        if (existingName != g_blobByName.end())
        {
            auto existingBlob = g_blobs.find(existingName->second);
            if (existingBlob != g_blobs.end() && existingBlob->second.size == size && existingBlob->second.view)
            {
                ++existingBlob->second.refs;
                ++g_blobRefs;
                reused = true;
                return existingName->second;
            }
            g_blobByName.erase(existingName);
        }

        uint32_t id = ++g_nextHandle;
        if (id == 0) id = ++g_nextHandle; // ids must be nonzero

        char nm[64];
        BlobName(nm, sizeof(nm), id);
        HANDLE h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size ? size : 1, nm);
        if (!h) return 0;
        void* v = MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, size);
        if (!v) { CloseHandle(h); return 0; }
        if (size) memcpy(v, bytes.data(), size);

        g_blobs.emplace(id, Blob{ h, v, size, 1, name });
        g_blobByName.emplace(name, id);
        g_blobBytes += size;
        ++g_blobRefs;
        return id;
    }

    bool EndsWithCI(std::string_view s, const char* suffix)
    {
        const size_t ls = std::strlen(suffix);
        if (ls > s.size()) return false;
        for (size_t i = 0; i < ls; ++i)
        {
            const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[s.size() - ls + i])));
            const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
            if (a != b) return false;
        }
        return true;
    }

    bool StartsWithCI(std::string_view s, const char* prefix)
    {
        const size_t lp = std::strlen(prefix);
        if (lp > s.size()) return false;
        for (size_t i = 0; i < lp; ++i)
        {
            const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
            const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(prefix[i])));
            if (a != b) return false;
        }
        return true;
    }

    std::string NameKey(std::string_view name)
    {
        std::string key(name);
        for (char& c : key)
            c = (c == '/') ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return key;
    }

    void AddUniqueAlias(std::vector<std::string>& aliases, const std::string& original, std::string alias)
    {
        if (alias.empty() || alias == original) return;
        for (const std::string& existing : aliases)
            if (existing == alias) return;
        aliases.emplace_back(std::move(alias));
    }

    void AppendTextureComponentAliases(const std::string& name, std::vector<std::string>& aliases)
    {
        const std::string key = NameKey(name);
        if (!StartsWithCI(key, "item\\texturecomponents\\")) return;
        if (!EndsWithCI(key, ".blp") && !EndsWithCI(key, ".tga")) return;

        const size_t dot = name.find_last_of('.');
        if (dot == std::string::npos || dot < 2) return;

        const char gender = static_cast<char>(std::tolower(static_cast<unsigned char>(name[dot - 1])));
        if (name[dot - 2] != '_' || (gender != 'f' && gender != 'm')) return;

        std::string unsuffixed = name;
        unsuffixed.erase(dot - 2, 2);

        const std::string stem = NameKey(std::string_view(name).substr(0, dot - 2));
        bool hasUnisexMarker = EndsWithCI(stem, "_u");
        const size_t u = stem.rfind("_u_");
        if (!hasUnisexMarker && u != std::string::npos)
        {
            hasUnisexMarker = true;
            for (size_t i = u + 3; i < stem.size(); ++i)
            {
                if (stem[i] < '0' || stem[i] > '9')
                {
                    hasUnisexMarker = false;
                    break;
                }
            }
        }

        if (hasUnisexMarker)
            AddUniqueAlias(aliases, name, unsuffixed);
        else
        {
            std::string unisex = name;
            unisex[dot - 1] = 'u';
            AddUniqueAlias(aliases, name, std::move(unisex));
        }

        std::string opposite = name;
        opposite[dot - 1] = (gender == 'm') ? 'f' : 'm';
        AddUniqueAlias(aliases, name, std::move(opposite));
        AddUniqueAlias(aliases, name, std::move(unsuffixed));
    }

    void AppendSemicolonTextureAliases(const std::string& name, std::vector<std::string>& aliases)
    {
        if (!EndsWithCI(name, ".blp") && !EndsWithCI(name, ".tga")) return;

        const size_t dot = name.find_last_of('.');
        if (dot == std::string::npos) return;

        const size_t slash = name.find_last_of("\\/");
        const size_t fileStart = (slash == std::string::npos) ? 0 : slash + 1;
        const size_t semicolon = name.find(';', fileStart);
        if (semicolon == std::string::npos || semicolon >= dot) return;

        const std::string prefix = name.substr(0, fileStart);
        const std::string extension = name.substr(dot);
        const std::string key = NameKey(name);
        const bool objectComponent =
            StartsWithCI(key, "item\\objectcomponents\\") &&
            !StartsWithCI(key, "item\\objectcomponents\\collections\\");

        size_t partStart = fileStart;
        for (;;)
        {
            size_t rawEnd = name.find(';', partStart);
            if (rawEnd == std::string::npos || rawEnd > dot) rawEnd = dot;
            size_t partEnd = rawEnd;

            while (partStart < partEnd && (name[partStart] == ' ' || name[partStart] == '\t'))
                ++partStart;
            while (partEnd > partStart && (name[partEnd - 1] == ' ' || name[partEnd - 1] == '\t'))
                --partEnd;

            if (partEnd > partStart)
            {
                const std::string stem = name.substr(partStart, partEnd - partStart);
                AddUniqueAlias(aliases, name, prefix + stem + extension);
                if (objectComponent)
                    AddUniqueAlias(aliases, name, std::string("Item\\ObjectComponents\\Collections\\") + stem + extension);
            }

            if (rawEnd >= dot) break;
            partStart = rawEnd + 1;
        }
    }

    void AppendObjectComponentRaceGenderAliases(const std::string& name, std::vector<std::string>& aliases)
    {
        const std::string key = NameKey(name);
        if (!StartsWithCI(key, "item\\objectcomponents\\")) return;
        if (!EndsWithCI(key, ".m2") && !EndsWithCI(key, ".mdx") && !EndsWithCI(key, ".skin")) return;

        size_t dot = key.find_last_of('.');
        if (dot == std::string::npos) return;

        size_t suffixEnd = dot;
        if (EndsWithCI(key, ".skin") && dot >= 2 &&
            key[dot - 1] >= '0' && key[dot - 1] <= '9' &&
            key[dot - 2] >= '0' && key[dot - 2] <= '9')
        {
            suffixEnd = dot - 2;
        }

        const size_t slash = key.find_last_of('\\');
        const size_t base = (slash == std::string::npos) ? 0 : slash + 1;
        auto isRace = [](char a, char b) {
            return a >= 'a' && a <= 'z' && b >= 'a' && b <= 'z';
        };
        auto isGender = [](char c) {
            return c == 'm' || c == 'f';
        };
        std::vector<std::string> candidates;
        candidates.emplace_back(name);
        auto addDerivedAlias = [&](std::string alias) {
            if (alias.empty() || alias == name) return;
            for (const std::string& candidate : candidates)
                if (candidate == alias) return;
            for (const std::string& existing : aliases)
                if (existing == alias) return;
            aliases.emplace_back(alias);
            candidates.emplace_back(std::move(alias));
        };

        if (suffixEnd >= base + 4)
        {
            const size_t s = suffixEnd - 4;
            if (s > base && key[s - 1] == '_' && isRace(key[s], key[s + 1]) &&
                key[s + 2] == '_' && isGender(key[s + 3]))
            {
                std::string alias = name;
                alias.erase(s + 2, 1);
                addDerivedAlias(std::move(alias));

                alias = name;
                alias.erase(s - 1, suffixEnd - (s - 1));
                addDerivedAlias(std::move(alias));
            }
        }

        if (suffixEnd >= base + 3)
        {
            const size_t s = suffixEnd - 3;
            if (s > base && key[s - 1] == '_' && isRace(key[s], key[s + 1]) && isGender(key[s + 2]))
            {
                std::string alias = name;
                alias.insert(s + 2, 1, '_');
                addDerivedAlias(std::move(alias));

                alias = name;
                alias.erase(s - 1, suffixEnd - (s - 1));
                addDerivedAlias(std::move(alias));
            }
        }

        auto addModelExtensionAlias = [&](const std::string& candidate) {
            const std::string candidateKey = NameKey(candidate);
            const size_t ext = candidate.find_last_of('.');
            if (ext == std::string::npos) return;

            if (EndsWithCI(candidateKey, ".m2"))
            {
                std::string alias = candidate;
                alias.replace(ext, std::string::npos, ".mdx");
                addDerivedAlias(std::move(alias));
            }
            else if (EndsWithCI(candidateKey, ".mdx"))
            {
                std::string alias = candidate;
                alias.replace(ext, std::string::npos, ".m2");
                addDerivedAlias(std::move(alias));
            }
        };

        auto addCollectionAlias = [&](const std::string& candidate) {
            constexpr const char* kObjectPrefix = "item\\objectcomponents\\";
            constexpr const char* kCollectionsPrefix = "Item\\ObjectComponents\\Collections\\";

            const std::string candidateKey = NameKey(candidate);
            if (!StartsWithCI(candidateKey, kObjectPrefix)) return;
            if (StartsWithCI(candidateKey, "item\\objectcomponents\\collections\\")) return;
            if (StartsWithCI(candidateKey, "item\\objectcomponents\\collection\\")) return;

            const size_t folderStart = std::strlen(kObjectPrefix);
            const size_t fileStart = candidateKey.find('\\', folderStart);
            if (fileStart == std::string::npos || fileStart + 1 >= candidateKey.size()) return;
            const size_t stemEnd = candidateKey.find_last_of('.');
            if (stemEnd == std::string::npos || stemEnd <= fileStart + 1) return;

            std::string alias = candidate;
            alias.replace(0, fileStart + 1, kCollectionsPrefix);
            addDerivedAlias(std::move(alias));
        };

        for (size_t i = 0; i < candidates.size(); ++i)
        {
            addCollectionAlias(candidates[i]);
            addModelExtensionAlias(candidates[i]);
        }
    }

    void AppendObjectComponentTextureAliases(const std::string& name, std::vector<std::string>& aliases)
    {
        const std::string key = NameKey(name);
        if (!StartsWithCI(key, "item\\objectcomponents\\")) return;
        if (StartsWithCI(key, "item\\objectcomponents\\collections\\")) return;
        if (StartsWithCI(key, "item\\objectcomponents\\collection\\")) return;
        if (!EndsWithCI(key, ".blp") && !EndsWithCI(key, ".tga")) return;

        constexpr const char* kObjectPrefix = "item\\objectcomponents\\";
        constexpr const char* kCollectionsPrefix = "Item\\ObjectComponents\\Collections\\";
        const size_t folderStart = std::strlen(kObjectPrefix);
        const size_t fileStart = key.find('\\', folderStart);
        if (fileStart == std::string::npos || fileStart + 1 >= key.size()) return;
        const size_t stemEnd = key.find_last_of('.');
        if (stemEnd == std::string::npos || stemEnd <= fileStart + 1) return;

        std::string alias = name;
        alias.replace(0, fileStart + 1, kCollectionsPrefix);
        AddUniqueAlias(aliases, name, std::move(alias));

        constexpr const char* kCollectionPrefix = "Item\\ObjectComponents\\Collection\\";
        alias = name;
        alias.replace(0, fileStart + 1, kCollectionPrefix);
        AddUniqueAlias(aliases, name, std::move(alias));
    }

    /**
     * @brief Builds the file-open response: inline bytes when small, otherwise a zero-copy section id.
     * @param fbb    FlexBuffers builder receiving the response
     * @param name   file name (logging)
     * @param bytes  the file bytes to return
     * @param trace  per-open counters and stage timings
     */
    void RespondWithFile(flexbuffers::Builder& fbb, const char* name, std::vector<uint8_t>&& bytes,
                         hprof::OpenTrace& trace)
    {
        uint32_t size = static_cast<uint32_t>(bytes.size());
        if (size > kInlineMax)
        {
            const uint64_t started = hprof::Now();
            bool reused = false;
            uint32_t id = StoreBlob(name ? std::string(name) : std::string(), bytes, reused);
            trace.blobTicks += hprof::Now() - started;
            if (id != 0)
            {
                trace.ok = true;
                trace.bytes = size;
                trace.sharedBytes += size;
                if (reused) ++trace.blobReuses;
                else        ++trace.blobCreates;
                fbb.Vector([&]() { fbb.UInt(StOk); fbb.UInt(id); fbb.UInt(size); });
                HOST_OPEN_CONSOLE("open  %-44s -> OK id=%u (%u B)\n", name, id, size);
                return;
            }
            ++trace.blobFailures;
            fbb.Vector([&]() { fbb.UInt(StNotFound); fbb.UInt(0); fbb.UInt(0); });
            HOST_OPEN_CONSOLE("open  %-44s -> FAIL (no section, %u B)\n", name, size);
            return;
        }

        // Inline: bytes come back in the open response.
        trace.ok = true;
        trace.bytes = size;
        trace.inlineBytes += size;
        ++trace.inlineResponses;
        fbb.Vector([&]() { fbb.UInt(StOk); fbb.UInt(0); fbb.UInt(size); fbb.Blob(bytes.data(), bytes.size()); });
        HOST_OPEN_CONSOLE("open  %-44s -> OK inline (%u B)\n", name, size);
    }

    bool TryTransformCache(const std::string& name, std::vector<uint8_t>& out, hprof::OpenTrace& trace)
    {
        ++trace.transformCacheLookups;
        const uint64_t started = hprof::Now();
        bool hit = false;
        {
            std::lock_guard<std::mutex> lock(g_transformCacheMutex);
            auto it = g_transformCache.find(name);
            if (it != g_transformCache.end())
            {
                // Keep assets used by the current world resident. Without eviction the cache became a
                // write-once 128 MB bucket: after it filled, every transform for a newly teleported-to area
                // was repeated until the host restarted.
                g_transformCacheLru.splice(g_transformCacheLru.end(), g_transformCacheLru, it->second.lru);
                out = it->second.bytes;
                hit = true;
            }
        }
        trace.transformCacheTicks += hprof::Now() - started;
        if (hit) ++trace.transformCacheHits;
        return hit;
    }

    void RememberTransform(const std::string& name, const std::vector<uint8_t>& bytes, hprof::OpenTrace& trace)
    {
        if (bytes.empty() || bytes.size() > TransformCacheMaxEntry()) return;

        const uint64_t started = hprof::Now();
        std::lock_guard<std::mutex> lock(g_transformCacheMutex);
        if (g_transformCache.find(name) == g_transformCache.end())
        {
            auto pendingLru = g_transformCacheLru.end();
            try
            {
                const size_t limit = TransformCacheMaxBytes();
                while (!g_transformCacheLru.empty() && g_transformCacheBytes + bytes.size() > limit)
                {
                    auto oldest = g_transformCache.find(g_transformCacheLru.front());
                    if (oldest != g_transformCache.end())
                    {
                        g_transformCacheBytes -= oldest->second.bytes.size();
                        g_transformCache.erase(oldest);
                    }
                    g_transformCacheLru.pop_front();
                }

                pendingLru = g_transformCacheLru.insert(g_transformCacheLru.end(), name);
                auto [it, inserted] = g_transformCache.emplace(name, TransformCacheEntry{ bytes, pendingLru });
                (void)it;
                if (inserted)
                {
                    g_transformCacheBytes += bytes.size();
                    ++trace.transformCacheStores;
                    pendingLru = g_transformCacheLru.end();
                }
                else
                {
                    g_transformCacheLru.erase(pendingLru);
                    pendingLru = g_transformCacheLru.end();
                }
            }
            catch (...)
            {
                if (pendingLru != g_transformCacheLru.end())
                    g_transformCacheLru.erase(pendingLru);
                static bool logged = false;
                if (!logged)
                {
                    logged = true;
                    wlog::Printf("host: transform cache allocation failed; continuing uncached");
                }
            }
        }
        trace.transformCacheTicks += hprof::Now() - started;
    }

    /**
     * @brief Reads raw archive bytes for `name`, offers them to the transform hooks, else passes them through.
     * @param name  archive-internal file name
     * @param out   receives the reshaped or raw bytes
     * @return false only on an archive miss; provider hooks are fired by the caller, not here
     */
    bool ProduceCandidate(const std::string& requestName, const std::string& readName,
                          std::vector<uint8_t>& out, hprof::OpenTrace& trace)
    {
        if (TryTransformCache(readName, out, trace))
        {
            if (readName != requestName) RememberTransform(requestName, out, trace);
            return true;
        }

        std::vector<uint8_t> raw;
        ++trace.archiveReads;
        const uint64_t archiveStarted = hprof::Now();
        const bool archiveHit = MpqReadAll(readName, raw);
        trace.archiveTicks += hprof::Now() - archiveStarted;
        if (!archiveHit)
        {
            ++trace.archiveMisses;
            return false;
        }

        std::vector<uint8_t> reshaped;
        ++trace.transformCalls;
        const uint64_t transformStarted = hprof::Now();
        const bool transformed = wxl::host::Transform(readName, raw, reshaped);
        trace.transformTicks += hprof::Now() - transformStarted;
        if (transformed)
        {
            ++trace.transformClaims;
            RememberTransform(readName, reshaped, trace);
            if (readName != requestName) RememberTransform(requestName, reshaped, trace);
            out = std::move(reshaped);
            return true;
        }
        out = std::move(raw);
        return true;
    }

    bool ProduceServed(const std::string& name, std::vector<uint8_t>& out, hprof::OpenTrace& trace)
    {
        if (TryTransformCache(name, out, trace))
            return true;

        if (ProduceCandidate(name, name, out, trace))
            return true;

        std::vector<std::string> aliases;
        AppendSemicolonTextureAliases(name, aliases);
        AppendObjectComponentRaceGenderAliases(name, aliases);
        AppendObjectComponentTextureAliases(name, aliases);
        AppendTextureComponentAliases(name, aliases);
        for (const std::string& alias : aliases)
        {
            ++trace.aliasesTried;
            ++trace.providerCalls;
            const uint64_t providerStarted = hprof::Now();
            const bool provided = wxl::host::Provide(alias, out);
            trace.providerTicks += hprof::Now() - providerStarted;
            if (provided)
            {
                ++trace.providerHits;
                return true;
            }
            if (ProduceCandidate(name, alias, out, trace))
                return true;
        }

        return false;
    }

    /**
     * @brief Serves a file-open request: tries the providers, then the archive read and transforms.
     * @param fbb   FlexBuffers builder receiving the response
     * @param name  file name requested
     * @param trace receives the open-stage measurements and outcome
     */
    void HandleFileOpen(flexbuffers::Builder& fbb, const std::string& name, hprof::OpenTrace& trace)
    {
        std::vector<uint8_t> provided;
        ++trace.providerCalls;
        const uint64_t providerStarted = hprof::Now();
        const bool providerHit = wxl::host::Provide(name, provided);
        trace.providerTicks += hprof::Now() - providerStarted;
        if (providerHit)
        {
            ++trace.providerHits;
            RespondWithFile(fbb, name.c_str(), std::move(provided), trace);
            return;
        }

        std::vector<uint8_t> served;
        if (!ProduceServed(name, served, trace))
        {
            trace.miss = true;
            fbb.Vector([&]() { fbb.UInt(StNotFound); fbb.UInt(0); fbb.UInt(0); });
            HOST_OPEN_CONSOLE("open  %-44s -> MISS\n", name.c_str());
            return;
        }

        ++trace.servedCalls;
        const uint64_t servedStarted = hprof::Now();
        wxl::host::NotifyServed(name, served);
        trace.servedTicks += hprof::Now() - servedStarted;
        RespondWithFile(fbb, name.c_str(), std::move(served), trace);
    }

    /**
     * @brief Serves a fallback range read for a client that cannot map the blob section directly.
     * @param fbb  FlexBuffers builder receiving the response
     * @param vec  request vector holding blob id, offset, and length
     * @param trace receives transfer bytes and invalid-id status
     */
    void HandleFileRead(flexbuffers::Builder& fbb, const flexbuffers::Vector& vec,
                        hprof::RequestTrace& trace)
    {
        uint32_t id = vec[1].AsUInt32(), off = vec[2].AsUInt32(), len = vec[3].AsUInt32();
        if (len > kFileChunkMax) len = kFileChunkMax;

        std::vector<uint8_t> out;
        bool found = false;
        {
            std::lock_guard<std::mutex> hl(g_handleMutex);
            auto it = g_blobs.find(id);
            if (it != g_blobs.end())
            {
                found = true;
                const Blob& b = it->second;
                if (off < b.size && b.view)
                {
                    uint32_t avail = b.size - off;
                    uint32_t n = len < avail ? len : avail;
                    const uint8_t* src = static_cast<const uint8_t*>(b.view) + off;
                    out.assign(src, src + n);
                }
            }
        }
        trace.ok = found;
        trace.badRequest = !found;
        trace.bytes = out.size();
        if (found) fbb.Vector([&]() { fbb.UInt(StOk); fbb.Blob(out.data(), out.size()); });
        else       fbb.Vector([&]() { fbb.UInt(StBadRequest); fbb.Blob(nullptr, 0); });
    }

    /**
     * @brief Serves a file-close request: unmaps and releases the blob section for `id`.
     * @param fbb  FlexBuffers builder receiving the response
     * @param id   blob id to release
     */
    void HandleFileClose(flexbuffers::Builder& fbb, uint32_t id, hprof::RequestTrace& trace)
    {
        trace.ok = true;
        std::lock_guard<std::mutex> hl(g_handleMutex);
        auto it = g_blobs.find(id);
        if (it != g_blobs.end())
        {
            if (it->second.refs > 1)
            {
                --it->second.refs;
                --g_blobRefs;
                fbb.Vector([&]() { fbb.UInt(StOk); });
                return;
            }
            auto nameIt = g_blobByName.find(it->second.name);
            if (nameIt != g_blobByName.end() && nameIt->second == id)
                g_blobByName.erase(nameIt);
            g_blobBytes -= it->second.size;
            --g_blobRefs;
            if (it->second.view)    UnmapViewOfFile(it->second.view);
            if (it->second.section) CloseHandle(it->second.section);
            g_blobs.erase(it);
        }
        fbb.Vector([&]() { fbb.UInt(StOk); });
    }

    /**
     * @brief Decodes one request payload and builds the FlexBuffers response.
     * @param req      request payload bytes
     * @param respOut  receives the response payload bytes
     * @param trace    receives decoded operation, outcome, and open-stage data
     */
    void ProcessRequest(const std::vector<uint8_t>& req, std::vector<uint8_t>& respOut,
                        hprof::RequestTrace& trace)
    {
        flexbuffers::Builder fbb;
        if (!req.empty())
        {
            auto vec = flexbuffers::GetRoot(req.data(), req.size()).AsVector();
            uint32_t op = vec[0].AsUInt32();
            switch (op)
            {
            case OpFileOpen:
                trace.op = hprof::RequestOp::FileOpen;
                trace.name = vec[1].AsString().str();
                HandleFileOpen(fbb, trace.name, trace.open);
                trace.ok = trace.open.ok;
                trace.bytes = trace.open.bytes;
                break;
            case OpFileRead:
                trace.op = hprof::RequestOp::FileRead;
                HandleFileRead(fbb, vec, trace);
                break;
            case OpFileClose:
                trace.op = hprof::RequestOp::FileClose;
                HandleFileClose(fbb, vec[1].AsUInt32(), trace);
                break;
            case OpFileExists:
            {
                trace.op = hprof::RequestOp::FileExists;
                std::string name = vec[1].AsString().str();
                bool ok = MpqExists(name) || wxl::host::Exists(name);
                trace.ok = ok;
                fbb.Vector([&]() { fbb.UInt(ok ? StOk : StNotFound); });
                break;
            }
            default:
                trace.badRequest = true;
                fbb.Vector([&]() { fbb.UInt(StBadRequest); });
                break;
            }
        }
        else
        {
            trace.badRequest = true;
            fbb.Vector([&]() { fbb.UInt(StBadRequest); });
        }

        fbb.Finish();
        const std::vector<uint8_t>& buf = fbb.GetBuffer();
        respOut.assign(buf.begin(), buf.end());
    }

    /** @brief Samples transform-cache and live shared-section memory for a periodic profile report. */
    hprof::Gauges SnapshotProfileGauges()
    {
        hprof::Gauges gauges;
        {
            std::lock_guard<std::mutex> lock(g_transformCacheMutex);
            gauges.transformCacheEntries = g_transformCache.size();
            gauges.transformCacheBytes = g_transformCacheBytes;
        }
        {
            std::lock_guard<std::mutex> lock(g_handleMutex);
            gauges.blobs = g_blobs.size();
            gauges.blobBytes = g_blobBytes;
            gauges.blobRefs = g_blobRefs;
        }
        return gauges;
    }

    /**
     * @brief Mounts the archives, creates the transport, and runs the request loop until the client closes.
     * @return process exit code
     */
    int Serve()
    {
        // One host per session.
        HANDLE singleton = CreateMutexA(nullptr, FALSE, "Local\\WarcraftXLHostSingleton");
        if (singleton && GetLastError() == ERROR_ALREADY_EXISTS)
        {
            wlog::Printf("host: another instance is running, exiting");
            return 0;
        }

        if (g_clientPid)
        {
            HANDLE client = OpenProcess(SYNCHRONIZE, FALSE, g_clientPid);
            if (client) CreateThread(nullptr, 0, ClientWatcher, client, 0, nullptr);
        }

        g_clientRoot = ClientRoot();
        wxl::host::SetClientRoot(g_clientRoot);

        g_mpq = std::make_unique<MpqStore>();
        g_mpq->Mount(g_clientRoot);

        // List the registered hooks (module host faces self-registered before main ran).
        wxl::host::LogRegisteredHandlers();

        // Resolver tables are intentionally lazy because the client root is unknown during static init.
        // Give them a background head start now; ordinary archive requests remain responsive, and by the
        // time the first HD model needs an FDID the several-second cold table load is normally complete.
        if (HANDLE warmer = CreateThread(nullptr, 0, ResolverWarmer, nullptr, 0, nullptr))
            CloseHandle(warmer);

        if (!wxl::host::ipc::Create())
        {
            wlog::Printf("host: ShmServer.Create failed (err %lu)", GetLastError());
            return 1;
        }

        if (g_console) SetConsoleTitleA("WarcraftXLHost  -  client <-> host IPC");
        HOST_CONSOLE("WarcraftXLHost serving. Waiting for requests...\n\n");
        const uint32_t channels = wxl::host::ipc::ChannelCount(); // chosen by Create() from hardware_concurrency
        const uint32_t workerCount = ComputeWorkerCount(channels);
        wlog::Printf("host: serving (%u channels, %u worker thread(s))", channels, workerCount);

        // Fewer worker threads than channels: channels bound how many requests the client can have in
        // flight at once, workers bound how many Transforms actually run concurrently on this machine's
        // cores. Every worker waits on the full channel set (WaitAnyRequest) and picks up whichever one
        // is free, rather than one thread being tied to one channel -- so a burst larger than the worker
        // count still queues on the (idle, cheap) channel events instead of spinning up one CPU-bound
        // thread per logical core and starving the game client running on the same machine.
        auto worker = []() {
            std::vector<uint8_t> req, resp;
            for (;;)
            {
                uint32_t ch = 0, reqSeq = 0;
                if (!wxl::host::ipc::WaitAnyRequest(ch, reqSeq, req)) break;
                hprof::RequestTrace trace;
                const uint64_t requestStarted = hprof::Now();
                ProcessRequest(req, resp, trace);
                const uint64_t postStarted = hprof::Now();
                wxl::host::ipc::PostResponse(ch, reqSeq, resp);
                const uint64_t requestFinished = hprof::Now();

                // Profile.cpp keeps a compact non-atomic window. Serialize only its bookkeeping; archive
                // reads and transforms remain fully concurrent across the worker pool.
                std::lock_guard<std::mutex> profileLock(g_profileMutex);
                hprof::RecordRequest(trace, requestFinished - requestStarted,
                                     requestFinished - postStarted);
                if (hprof::ReportDue())
                {
                    hprof::Report(SnapshotProfileGauges());
                    wxl::host::LogAndResetHandlerProfile();
                }
            }
        };

        std::vector<std::thread> workers;
        for (uint32_t w = 1; w < workerCount; ++w)
            workers.emplace_back(worker);

        worker(); // this thread is also a pool worker; its loop only ends at process teardown
        for (auto& t : workers) t.join();
        return 0;
    }
}

namespace
{
    /**
     * @brief Resolves one asset through the serve pipeline and writes the bytes to a file.
     *        Reuses the process-wide host state, so a later call in the same process (e.g. a
     *        texture request after its model) sees whatever the earlier call registered.
     * @param name  asset path as the client would request it (lowercase backslash form)
     * @param dest  output file receiving the served bytes
     * @return process exit code (0 = served and written)
     */
    int ProvideToFile(const std::string& name, const std::string& dest)
    {
        std::vector<uint8_t> out;
        hprof::OpenTrace trace;
        if (!wxl::host::Provide(name, out) && !ProduceServed(name, out, trace))
        {
            wlog::Printf("provide: %s -> MISS", name.c_str());
            fprintf(stderr, "MISS %s\n", name.c_str());
            return 1;
        }

        FILE* f = fopen(dest.c_str(), "wb");
        if (!f)
        {
            fprintf(stderr, "cannot write %s\n", dest.c_str());
            return 2;
        }
        fwrite(out.data(), 1, out.size(), f);
        fclose(f);
        wlog::Printf("provide: %s -> %u bytes -> %s", name.c_str(),
                     static_cast<uint32_t>(out.size()), dest.c_str());
        printf("OK %u bytes -> %s\n", static_cast<uint32_t>(out.size()), dest.c_str());
        return 0;
    }
}

/**
 * @brief Parses command-line arguments, opens the log, and runs the serve loop.
 * @param argc  argument count
 * @param argv  argument values (--data, --client-pid, --console,
 *              --provide NAME --out FILE [--provide NAME2 --out FILE2 ...])
 *              repeated --provide/--out pairs run in the same process, in order, so a later
 *              request (e.g. a texture) sees state an earlier one (e.g. its model) registered;
 *              --console-opens enables per-open console output
 * @return process exit code
 */
int main(int argc, char** argv)
{
    g_dataDir = ExeDir();

    std::vector<std::pair<std::string, std::string>> provides; // (name, out)
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) g_dataDir = argv[++i];
        else if (a == "--client-pid" && i + 1 < argc) g_clientPid = static_cast<DWORD>(strtoul(argv[++i], nullptr, 10));
        else if (a == "--console") g_console = true;
        else if (a == "--provide" && i + 1 < argc) provides.push_back({ argv[++i], "provided.bin" });
        else if (a == "--out" && i + 1 < argc && !provides.empty()) provides.back().second = argv[++i];
        else if (a == "--console-opens") g_consoleOpenLog = true;
    }
    if (!g_consoleOpenLog)
        g_consoleOpenLog = ConsoleOpenLogRequested();

    wlog::Open((g_dataDir + "\\WarcraftXLHost.log").c_str());
    wlog::Printf("WarcraftXLHost starting (build %s %s)", __DATE__, __TIME__);
    hprof::LogSettings();
    wlog::Printf("host: transform cache limit=%zu MB entry=%zu MB consoleOpenLog=%u",
                 TransformCacheMaxBytes() / (1024 * 1024),
                 TransformCacheMaxEntry() / (1024 * 1024),
                 g_consoleOpenLog ? 1u : 0u);
    int rc = 0;
    if (!provides.empty())
    {
        g_clientRoot = ClientRoot();
        wxl::host::SetClientRoot(g_clientRoot);
        g_mpq = std::make_unique<MpqStore>();
        g_mpq->Mount(g_clientRoot);
        wxl::host::LogRegisteredHandlers();
        for (const auto& [name, dest] : provides)
        {
            rc = ProvideToFile(name, dest);
            if (rc) break;
        }
    }
    else
        rc = Serve();
    wlog::Close();
    return rc;
}
