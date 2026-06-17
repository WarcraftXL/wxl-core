// Copyright (C) 2026 WraithEngine
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

// HOST (64-bit). Stateless asset translator + MPQ/DB2 owner. The client serializes its
// opens onto a single channel, so the host serves them on its main thread; a background
// prefetch pool is the parallel translation lane that warms the byte cache. Each store
// (the serve store and the per-prefetch-thread stores) is single-thread, so archive reads
// take no lock. Resolve tables (Db2Store) are read-only after load, so they are shared
// lock-free.
//
// The transport is request/response and imposes NO blocking model on the client: the host
// never waits on the client, so the DLL is free to run its round-trips on a background IO
// pump without any host change.
//
// Console output is opt-in at runtime via --console; the file log is always on.

#include "Protocol.hpp"
#include "ShmServer.hpp"
#include "StructureDispatcher.hpp"
#include "MpqStore.hpp"
#include "Db2Store.hpp"
#include "Logger.hpp"

#include <flatbuffers/flexbuffers.h>

#include <windows.h>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <sstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace wraith::ipc;
using wraith::host::mpq::MpqStore;
using wraith::host::db2::Db2Store;

// Console output is opt-in at runtime via --console (sets g_console); the file log is always on.
// A runtime gate, not a build switch, so a Release host can show the console on demand.
static bool g_console = false;
#define HOST_CONSOLE(...) do { if (g_console) printf(__VA_ARGS__); } while (0)

namespace
{
    std::string g_dataDir;     // client data root (defaults to the host exe's parent; --data overrides)
    DWORD       g_clientPid = 0; // game process to shadow; host exits when it closes

    // The serve store, used only by the main thread (no StormLib cross-thread locking). Mounted lazily
    // (see EnsureMounted). Sized by kChannels so it tracks the transport if the channel count ever grows.
    std::unique_ptr<MpqStore> g_mpq[kChannels];
    std::string g_clientRoot; // client data root; set in Serve, read by EnsureMounted

    // Resolution authority: read-only after Load, shared by all workers.
    Db2Store g_db2;

    // FileDataID -> texture path adapter for the translators (cold; once per unresolved reference).
    bool ResolveTextureThunk(void* user, uint32_t fileDataId, std::string& outPath)
    {
        return static_cast<Db2Store*>(user)->ResolveFile(fileDataId, outPath);
    }

    // Large/translated files are served zero-copy: the host copies the bytes once into a named shared
    // section and keeps the section/view alive until the client closes the id. Any worker can serve a
    // later OpFileClose/OpFileRead for any id. Guarded by g_handleMutex.
    struct Blob
    {
        HANDLE   section; // named section keeping the bytes alive
        void*    view;    // host-side writable view of the section
        uint32_t size;    // byte length
    };
    std::mutex g_handleMutex;
    std::unordered_map<uint32_t, Blob> g_blobs;
    uint32_t g_nextHandle = 0;

    std::mutex            g_printMutex; // serialises console lines across workers
    std::atomic<uint32_t> g_served{ 0 };

    DWORD WINAPI ClientWatcher(LPVOID clientHandle)
    {
        WaitForSingleObject(static_cast<HANDLE>(clientHandle), INFINITE);
        ExitProcess(0);
        return 0;
    }

    std::string ExeDir()
    {
        char p[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, p, MAX_PATH);
        std::string s(p, n);
        size_t slash = s.find_last_of("\\/");
        return (slash == std::string::npos) ? std::string(".") : s.substr(0, slash);
    }

    // The host runs from the client's Utils folder, so the client root is our parent.
    std::string ClientRoot()
    {
        std::string root = g_dataDir;
        size_t slash = root.find_last_of("\\/");
        if (slash != std::string::npos) root = root.substr(0, slash);
        return root;
    }

    // Copy whole bytes into a fresh named shared section and return its (nonzero) id. The bytes are
    // fully written into the section before the id is returned, so the open response doubles as the
    // ready signal: once the client sees the id, the mapping holds the complete file. Returns 0 if the
    // section or its view could not be created (caller falls back to inline/not-found).
    uint32_t StoreBlob(const std::vector<uint8_t>& bytes)
    {
        uint32_t size = static_cast<uint32_t>(bytes.size());

        std::lock_guard<std::mutex> hl(g_handleMutex);
        uint32_t id = ++g_nextHandle;
        if (id == 0) id = ++g_nextHandle; // ids must be nonzero

        char nm[64];
        BlobName(nm, sizeof(nm), id);
        HANDLE h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size ? size : 1, nm);
        if (!h) return 0;
        void* v = MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, size);
        if (!v) { CloseHandle(h); return 0; }
        if (size) memcpy(v, bytes.data(), size);

        g_blobs.emplace(id, Blob{ h, v, size });
        return id;
    }

    // Build the OpFileOpen response for `bytes`: inline when small, otherwise a zero-copy section id.
    void RespondWithFile(flexbuffers::Builder& fbb, uint32_t ch, const char* name, std::vector<uint8_t>&& bytes, const char* mode)
    {
        uint32_t size = static_cast<uint32_t>(bytes.size());
        uint32_t served = g_served.fetch_add(1) + 1;
        if (size > kInlineMax)
        {
            uint32_t id = StoreBlob(bytes);
            if (id != 0)
            {
                // Section is fully written above; the id is the ready signal.
                fbb.Vector([&]() { fbb.UInt(StOk); fbb.UInt(id); fbb.UInt(size); });
                std::lock_guard<std::mutex> pl(g_printMutex);
                HOST_CONSOLE("[#%u/c%u] open  %-44s -> OK %s id=%u (%u B)\n", served, ch, name, mode, id, size);
                return;
            }
            // Section creation failed: fall back to inline only if it now fits, else not found.
            if (size > kInlineMax)
            {
                fbb.Vector([&]() { fbb.UInt(StNotFound); fbb.UInt(0); fbb.UInt(0); });
                std::lock_guard<std::mutex> pl(g_printMutex);
                HOST_CONSOLE("[#%u/c%u] open  %-44s -> FAIL %s (no section, %u B)\n", served, ch, name, mode, size);
                return;
            }
        }

        // Inline: bytes come back in the open response (one small copy).
        fbb.Vector([&]() { fbb.UInt(StOk); fbb.UInt(0); fbb.UInt(size); fbb.Blob(bytes.data(), bytes.size()); });
        std::lock_guard<std::mutex> pl(g_printMutex);
        HOST_CONSOLE("[#%u/c%u] open  %-44s -> OK %s inline (%u B)\n", served, ch, name, mode, size);
    }

    // Translation cache: the bytes finally served for a name (translated or raw), kept across opens so a
    // re-open of the same asset skips the read + translate (the same WMO/M2/texture is opened many times
    // per zone). Archives are read-only -> no invalidation needed within a session. Bounded two ways: a
    // hard RAM ceiling (LRU eviction) AND a TTL swept opportunistically on the request flow: the game
    // opens assets constantly, so the open path IS the heartbeat (no timer thread). Guarded by g_cacheMutex.
    struct CacheEntry { std::vector<uint8_t> bytes; uint64_t lastAccess; bool fromPrefetch; bool used; };
    std::mutex g_cacheMutex;
    std::unordered_map<std::string, CacheEntry> g_cacheMap;
    size_t g_cacheBytes = 0;
    size_t   g_cacheCap        = 768ull * 1024 * 1024; // hard RAM ceiling (config: CacheCapMB)
    uint64_t g_cacheTtlMs      = 60u * 1000u;          // drop entries untouched this long (config: CacheTtlSec)
    uint64_t g_sweepIntervalMs = 2u * 1000u;           // min gap between TTL sweeps
    uint64_t g_lastSweep = 0;                          // last sweep tick (guarded by g_cacheMutex)
    // Prefetch validation counters (guarded by g_cacheMutex): assets prefetched, prefetched-then-used,
    // and prefetched-but-evicted-without-use (wasted). Confirms the prefetch is actually paying off.
    uint32_t g_pfFetched = 0, g_pfUsed = 0, g_pfWasted = 0;

    // Count a prefetch-origin entry dropped before it was ever used. Caller holds the lock.
    void NotePrefetchEvict(const CacheEntry& e) { if (e.fromPrefetch && !e.used) ++g_pfWasted; }

    // Throttled TTL sweep, driven by the request flow (the game opens assets constantly) rather than a
    // timer thread: drops entries untouched longer than the TTL, at most once per g_sweepIntervalMs, and
    // emits a periodic prefetch-effectiveness line. Caller holds g_cacheMutex.
    void SweepExpiredLocked()
    {
        const uint64_t now = GetTickCount64();
        if (now - g_lastSweep < g_sweepIntervalMs) return;
        g_lastSweep = now;
        for (auto it = g_cacheMap.begin(); it != g_cacheMap.end(); )
        {
            if (now - it->second.lastAccess > g_cacheTtlMs)
            { NotePrefetchEvict(it->second); g_cacheBytes -= it->second.bytes.size(); it = g_cacheMap.erase(it); }
            else ++it;
        }
        wraith::core::log::Printf("prefetch: fetched=%u used=%u wasted=%u | cache=%zu entries %zu MB",
            g_pfFetched, g_pfUsed, g_pfWasted, g_cacheMap.size(), g_cacheBytes / (1024 * 1024));
    }

    // Copy the cached bytes for name into out; true on hit (refreshes the entry's age; marks a prefetched
    // entry used on its first hit = proof a prefetched asset was actually consumed). Each lookup drives
    // the throttled TTL sweep. Caller responds outside the lock.
    bool CacheGet(const std::string& name, std::vector<uint8_t>& out)
    {
        std::lock_guard<std::mutex> cl(g_cacheMutex);
        SweepExpiredLocked();
        auto it = g_cacheMap.find(name);
        if (it == g_cacheMap.end()) return false;
        it->second.lastAccess = GetTickCount64();
        if (it->second.fromPrefetch && !it->second.used)
        {
            it->second.used = true;
            ++g_pfUsed;
            HOST_CONSOLE("[prefetch HIT] %s\n", name.c_str());
        }
        out = it->second.bytes;
        return true;
    }

    // Evict least-recently-used entries until `need` more bytes fit under the cap. Caller holds the lock.
    void CacheEvictToCapLocked(size_t need)
    {
        while (g_cacheBytes + need > g_cacheCap && !g_cacheMap.empty())
        {
            auto oldest = g_cacheMap.begin();
            for (auto it = g_cacheMap.begin(); it != g_cacheMap.end(); ++it)
                if (it->second.lastAccess < oldest->second.lastAccess) oldest = it;
            NotePrefetchEvict(oldest->second);
            g_cacheBytes -= oldest->second.bytes.size();
            g_cacheMap.erase(oldest);
        }
    }

    // Store a copy of the served bytes for name (skip if already present or larger than the cap).
    // fromPrefetch tags the entry so a later hit proves the prefetched bytes were used.
    void CachePut(const std::string& name, const std::vector<uint8_t>& bytes, bool fromPrefetch = false)
    {
        if (bytes.size() > g_cacheCap) return;
        std::lock_guard<std::mutex> cl(g_cacheMutex);
        if (g_cacheMap.count(name)) return;
        CacheEvictToCapLocked(bytes.size());
        g_cacheBytes += bytes.size();
        g_cacheMap.emplace(name, CacheEntry{ bytes, GetTickCount64(), fromPrefetch, false });
        if (fromPrefetch) ++g_pfFetched;
    }

    // Produce the final target-shaped bytes for `name` from `mpq`: ReadAll, then ADT multi-input merge,
    // single-input translation, or raw passthrough. Returns false only when the source is absent (a miss).
    // Pure: no cache, no response, no prefetch - reusable from both the channel workers and the prefetch
    // pool (each caller owns its own MpqStore). The byte result is identical to the original serve path.
    bool ProduceServed(MpqStore& mpq, const std::string& name, std::vector<uint8_t>& out)
    {
        wraith::structure::Format fmt = wraith::structure::Classify(name);

        std::vector<uint8_t> raw;
        if (!mpq.ReadAll(name, raw))
            return false;

        const wraith::structure::ResolveCtx rc{ &ResolveTextureThunk, &g_db2 };

        // ADT is a multi-input merge: `raw` is the split root; read the _tex0/_obj0 siblings and assemble
        // the monolithic tile. Absent siblings (a vanilla monolithic tile) -> serve the root raw. `rc`
        // resolves the modern texture FileDataIDs (MDID) the merged MTEX needs.
        if (fmt == wraith::structure::Format::Adt)
        {
            const std::string base = name.substr(0, name.size() - 4); // strip ".adt"
            std::vector<uint8_t> tex0, obj0;
            mpq.ReadAll(base + "_tex0.adt", tex0);
            mpq.ReadAll(base + "_obj0.adt", obj0);
            std::vector<uint8_t> merged;
            if (wraith::structure::adt::MergeSplitAdt(raw, tex0, obj0, merged, name, rc))
            {
                out = std::move(merged);
                return true;
            }
        }
        // Single-input translation (Wmo/Wdt/Wdl/M2). A false return means "needs no change" -> serve raw.
        else if (fmt != wraith::structure::Format::Raw)
        {
            std::vector<uint8_t> trans;
            const bool transformed = wraith::structure::Dispatch(name, raw, rc, trans);
            if (transformed)
            {
                out = std::move(trans);
                return true;
            }
        }

        out = std::move(raw);
        return true;
    }

    void SchedulePrefetch(const std::string& name, const std::vector<uint8_t>& served);

    void HandleFileOpen(flexbuffers::Builder& fbb, uint32_t ch, const std::string& name)
    {
        // Re-open of an already-served asset: skip the read + translate entirely. Deps were scheduled on
        // the first serve, so no SchedulePrefetch here.
        std::vector<uint8_t> cached;
        if (CacheGet(name, cached))
        {
            RespondWithFile(fbb, ch, name.c_str(), std::move(cached), "cache");
            return;
        }

        std::vector<uint8_t> served;
        if (!ProduceServed(*g_mpq[ch], name, served))
        {
            uint32_t n = g_served.fetch_add(1) + 1;
            fbb.Vector([&]() { fbb.UInt(StNotFound); fbb.UInt(0); fbb.UInt(0); });
            std::lock_guard<std::mutex> pl(g_printMutex);
            HOST_CONSOLE("[#%u/c%u] open  %-44s -> MISS\n", n, ch, name.c_str());
            return;
        }

        CachePut(name, served);
        SchedulePrefetch(name, served);
        RespondWithFile(fbb, ch, name.c_str(), std::move(served), "gen");
    }

    // Dependency prefetch. When a real asset is served, its direct dependencies (textures, models, WMO
    // groups) are parsed out of the served bytes and warmed into the byte cache on background threads, so
    // the client's later synchronous main-thread opens of those names hit the cache instead of stalling on
    // a read + translate. DEPTH 1: a prefetched asset is produced + cached but does NOT schedule its own
    // deps. The pool threads each own an MpqStore (StormLib never crosses threads) and only touch the cache
    // mutex briefly for get/put, so they never block or slow a channel worker's own serve.
    constexpr uint32_t kPrefetchThreadsMax = 16;     // upper bound for the per-thread store array
    uint32_t g_prefetchThreads  = 3;                 // active prefetch threads (config: PrefetchThreads, 1..max)
    size_t   g_prefetchQueueCap = 4096;              // drop new entries past this many pending (config: PrefetchQueueCap)

    std::mutex              g_prefetchMutex;
    std::condition_variable g_prefetchCv;
    std::deque<std::string> g_prefetchQueue;          // pending names
    std::unordered_set<std::string> g_prefetchSeen;   // queued-or-cached names (dedup)

    // Pre-pool MpqStore per prefetch thread, lazy-mounted on first use (StormLib is single-thread per store).
    std::unique_ptr<MpqStore> g_prefetchMpq[kPrefetchThreadsMax];

    // Queue `name` for prefetch if not already seen and not already cached. Caller need not hold any lock.
    void EnqueuePrefetch(const std::string& name)
    {
        if (name.empty()) return;
        {
            std::lock_guard<std::mutex> pl(g_prefetchMutex);
            if (g_prefetchQueue.size() >= g_prefetchQueueCap) return; // bounded; drop under pressure
            if (!g_prefetchSeen.insert(name).second) return;         // already queued or done
            g_prefetchQueue.push_back(name);
        }
        g_prefetchCv.notify_one();
    }

    // A candidate dep is enqueued only if it looks like a real archive path (has a path separator or a
    // known asset extension); empty/garbage strings from a misparse are skipped.
    bool LooksLikePath(const std::string& s)
    {
        if (s.empty() || s.size() > 260) return false;
        for (unsigned char c : s) if (c < 0x20) return false; // control byte -> not a clean string
        if (s.find('\\') != std::string::npos || s.find('/') != std::string::npos) return true;
        auto endsWith = [&](const char* ext) {
            size_t n = std::strlen(ext);
            if (s.size() < n) return false;
            for (size_t i = 0; i < n; ++i)
                if (std::tolower(static_cast<unsigned char>(s[s.size()-n+i])) != ext[i]) return false;
            return true;
        };
        return endsWith(".blp") || endsWith(".m2") || endsWith(".mdx") || endsWith(".wmo");
    }

    uint32_t Rd32le(const uint8_t* p) { return p[0] | (p[1]<<8) | (p[2]<<16) | (uint32_t(p[3])<<24); }

    // Reversed-4CC chunk tags as stored little-endian (matching the structure readers' magic style).
    constexpr uint32_t Tag(char a, char b, char c, char d)
    {
        return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b))<<8) |
               (uint32_t(uint8_t(c))<<16) | (uint32_t(uint8_t(d))<<24);
    }

    // Enqueue each NUL-terminated string packed in a name-blob payload (MTEX/MMDX/MWMO/MOTX/MODN style).
    void EnqueueNameBlob(const uint8_t* p, uint32_t len)
    {
        uint32_t i = 0;
        while (i < len)
        {
            uint32_t start = i;
            while (i < len && p[i] != 0) ++i;
            if (i > start)
            {
                std::string s(reinterpret_cast<const char*>(p + start), i - start);
                if (LooksLikePath(s)) EnqueuePrefetch(s);
            }
            ++i; // skip the NUL
        }
    }

    // ADT (monolithic): walk top-level chunks; pull texture/model/wmo dependency names from the name blobs.
    void ExtractAdtDeps(const uint8_t* buf, uint32_t len)
    {
        constexpr uint32_t kMTEX = Tag('M','T','E','X');
        constexpr uint32_t kMMDX = Tag('M','M','D','X');
        constexpr uint32_t kMWMO = Tag('M','W','M','O');
        uint32_t o = 0;
        while (o + 8 <= len)
        {
            uint32_t m = Rd32le(buf + o), sz = Rd32le(buf + o + 4);
            if (o + 8 + sz > len) break;
            if (m == kMTEX || m == kMMDX || m == kMWMO) EnqueueNameBlob(buf + o + 8, sz);
            o += 8 + sz;
        }
    }

    // WMO root: textures (MOTX) + doodad models (MODN), plus the per-group files derived from the group
    // count (MOHD nGroups, or the MOGI record count). Group name convention "<rootbase>_NNN.wmo".
    void ExtractWmoRootDeps(const std::string& name, const uint8_t* buf, uint32_t len)
    {
        constexpr uint32_t kMOHD = Tag('M','O','H','D');
        constexpr uint32_t kMOGI = Tag('M','O','G','I');
        constexpr uint32_t kMOTX = Tag('M','O','T','X');
        constexpr uint32_t kMODN = Tag('M','O','D','N');

        uint32_t groupCount = 0;
        uint32_t o = 0;
        while (o + 8 <= len)
        {
            uint32_t m = Rd32le(buf + o), sz = Rd32le(buf + o + 4);
            if (o + 8 + sz > len) break;
            const uint8_t* data = buf + o + 8;
            if (m == kMOHD && sz >= 0x08)            groupCount = Rd32le(data + 0x04); // nGroups
            else if (m == kMOGI && groupCount == 0)  groupCount = sz / 0x20;           // fallback
            else if (m == kMOTX || m == kMODN)       EnqueueNameBlob(data, sz);
            o += 8 + sz;
        }

        // Group files: strip ".wmo", append "_%03u.wmo".
        const std::string base = name.substr(0, name.size() - 4);
        for (uint32_t i = 0; i < groupCount && i < 512; ++i)
        {
            char suffix[16];
            std::snprintf(suffix, sizeof(suffix), "_%03u.wmo", i);
            EnqueuePrefetch(base + suffix);
        }
    }

    // M2 (target-shaped MD20): the skin file "<base>00.skin", plus any hardcoded (type 0) texture filenames
    // from the texture list. The texture list is at header+0x50 (count,offset); each record is
    // {u32 type, u32 flags, M2Array filename(count,offset)} = 0x10 bytes; type 0 carries an inline name.
    void ExtractM2Deps(const std::string& name, const uint8_t* buf, uint32_t len)
    {
        // Skin file: replace the ".m2"/".mdx" extension with "00.skin".
        size_t dot = name.find_last_of('.');
        if (dot != std::string::npos)
            EnqueuePrefetch(name.substr(0, dot) + "00.skin");

        if (len < 0x58) return;
        if (Rd32le(buf) != 0x3032444D /*'MD20'*/) return; // served bytes are de-chunked MD20

        const uint32_t texCount = Rd32le(buf + 0x50);
        const uint32_t texOfs   = Rd32le(buf + 0x54);
        if (texCount == 0 || texCount > 0x1000) return;
        if (static_cast<size_t>(texOfs) + static_cast<size_t>(texCount) * 0x10 > len) return;

        for (uint32_t i = 0; i < texCount; ++i)
        {
            const uint8_t* rec = buf + texOfs + i * 0x10;
            uint32_t type = Rd32le(rec + 0x00);
            if (type != 0) continue; // only type 0 = hardcoded inline filename
            uint32_t nameCount = Rd32le(rec + 0x08);
            uint32_t nameOfs   = Rd32le(rec + 0x0C);
            if (nameCount == 0 || nameOfs >= len) continue;
            uint32_t avail = len - nameOfs;
            uint32_t take = nameCount < avail ? nameCount : avail;
            const char* s = reinterpret_cast<const char*>(buf + nameOfs);
            uint32_t slen = 0; while (slen < take && s[slen] != 0) ++slen;
            std::string path(s, slen);
            if (LooksLikePath(path)) EnqueuePrefetch(path);
        }
    }

    // Classify `name`, parse the served bytes for direct deps, and enqueue each (depth 1). Resolution of
    // FDID-based references already happened inside ProduceServed, so served names are plain paths.
    void SchedulePrefetch(const std::string& name, const std::vector<uint8_t>& served)
    {
        if (served.empty()) return;
        const uint8_t* buf = served.data();
        const uint32_t len = static_cast<uint32_t>(served.size());

        switch (wraith::structure::Classify(name))
        {
        case wraith::structure::Format::Adt:
            ExtractAdtDeps(buf, len);
            break;
        case wraith::structure::Format::Wmo:
            // Only a root WMO carries deps; a "_NNN.wmo" group references nothing new to warm.
            if (!(name.size() >= 8 && name[name.size()-8] == '_'))
                ExtractWmoRootDeps(name, buf, len);
            break;
        case wraith::structure::Format::M2:
            ExtractM2Deps(name, buf, len);
            break;
        default:
            break;
        }
    }

    // Prefetch pool thread: own MpqStore, drain the queue, warm the byte cache (no blob, no response).
    DWORD WINAPI PrefetchWorker(LPVOID idx)
    {
        uint32_t slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(idx));
        g_prefetchMpq[slot] = std::make_unique<MpqStore>();
        g_prefetchMpq[slot]->Mount(g_clientRoot);

        for (;;)
        {
            std::string name;
            {
                std::unique_lock<std::mutex> pl(g_prefetchMutex);
                g_prefetchCv.wait(pl, [] { return !g_prefetchQueue.empty(); });
                name = std::move(g_prefetchQueue.front());
                g_prefetchQueue.pop_front();
            }

            // Already warm (a worker or another prefetch beat us to it): nothing to do.
            std::vector<uint8_t> have;
            if (CacheGet(name, have)) continue;

            std::vector<uint8_t> served;
            if (ProduceServed(*g_prefetchMpq[slot], name, served))
                CachePut(name, served, /*fromPrefetch=*/true); // depth 1: warm only, no recursion
        }
        return 0;
    }

    void HandleFileRead(flexbuffers::Builder& fbb, const flexbuffers::Vector& vec)
    {
        uint32_t id = vec[1].AsUInt32(), off = vec[2].AsUInt32(), len = vec[3].AsUInt32();
        if (len > kFileChunkMax) len = kFileChunkMax;

        // Fallback path for a client that cannot map the section: copy the requested range out of the
        // host-side view. The zero-copy client reads the mapping directly and never reaches here.
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
        if (found) fbb.Vector([&]() { fbb.UInt(StOk); fbb.Blob(out.data(), out.size()); });
        else       fbb.Vector([&]() { fbb.UInt(StBadRequest); fbb.Blob(nullptr, 0); });
    }

    void HandleResolve(flexbuffers::Builder& fbb, uint32_t ch, uint32_t op, const flexbuffers::Vector& vec)
    {
        uint32_t arg = vec[1].AsUInt32();
        uint32_t arg2 = vec.size() > 2 ? vec[2].AsUInt32() : 0;

        std::string path;
        bool ok = (op == OpResolveMaterial) ? g_db2.ResolveMaterial(arg, arg2, path)
                                            : g_db2.ResolveFile(arg, path);
        const char* opn = (op == OpResolveMaterial) ? "mat" : (op == OpResolveModel) ? "model" : "tex";
        uint32_t served = g_served.fetch_add(1) + 1;
        { std::lock_guard<std::mutex> pl(g_printMutex);
          HOST_CONSOLE("[#%u/c%u] %-5s arg=%-9u -> %s\n", served, ch, opn, arg, ok ? path.c_str() : "(not found)"); }
        fbb.Vector([&]() { fbb.UInt(ok ? StOk : StNotFound); fbb.String(ok ? path : std::string()); });
    }

    // Build the FlexBuffers response for one request payload.
    void ProcessRequest(uint32_t ch, const std::vector<uint8_t>& req, std::vector<uint8_t>& respOut)
    {
        flexbuffers::Builder fbb;
        if (!req.empty())
        {
            auto vec = flexbuffers::GetRoot(req.data(), req.size()).AsVector();
            uint32_t op = vec[0].AsUInt32();
            switch (op)
            {
            case OpFileOpen:   HandleFileOpen(fbb, ch, vec[1].AsString().str()); break;
            case OpFileRead:   HandleFileRead(fbb, vec); break;
            case OpFileClose:
            {
                uint32_t id = vec[1].AsUInt32();
                {
                    std::lock_guard<std::mutex> hl(g_handleMutex);
                    auto it = g_blobs.find(id);
                    if (it != g_blobs.end())
                    {
                        if (it->second.view)    UnmapViewOfFile(it->second.view);
                        if (it->second.section) CloseHandle(it->second.section);
                        g_blobs.erase(it);
                    }
                }
                fbb.Vector([&]() { fbb.UInt(StOk); });
                break;
            }
            case OpFileExists:
            {
                bool ok = g_mpq[ch]->Exists(vec[1].AsString().str());
                fbb.Vector([&]() { fbb.UInt(ok ? StOk : StNotFound); });
                break;
            }
            default: HandleResolve(fbb, ch, op, vec); break;
            }
        }
        else
        {
            fbb.Vector([&]() { fbb.UInt(StBadRequest); });
        }

        fbb.Finish();
        const std::vector<uint8_t>& buf = fbb.GetBuffer();
        respOut.assign(buf.begin(), buf.end());
    }

    // Mount the serve store on first use. Only the main thread touches it, so it needs no lock. Keeps
    // startup to one archive-set open.
    void EnsureMounted(uint32_t ch)
    {
        if (!g_mpq[ch])
        {
            g_mpq[ch] = std::make_unique<MpqStore>();
            g_mpq[ch]->Mount(g_clientRoot);
        }
    }

    // Worker ch: own this channel, serve requests until shutdown.
    DWORD WINAPI Worker(LPVOID idx)
    {
        uint32_t ch = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(idx));
        std::vector<uint8_t> req, resp;
        for (;;)
        {
            if (!wraith::host::ipc::WaitRequest(ch, req)) break;
            EnsureMounted(ch);
            ProcessRequest(ch, req, resp);
            wraith::host::ipc::PostResponse(ch, resp);
        }
        return 0;
    }

    // Read tunables from <host exe dir>\Wraith.cfg if present: "key value" per line, '#' starts a comment.
    void LoadConfig()
    {
        std::ifstream f(ExeDir() + "\\Wraith.cfg");
        if (!f) return;
        std::string line;
        while (std::getline(f, line))
        {
            const size_t h = line.find('#');
            if (h != std::string::npos) line.erase(h);
            std::istringstream is(line);
            std::string k; unsigned long long v;
            if (!(is >> k >> v)) continue;
            if      (k == "CacheCapMB")       g_cacheCap = static_cast<size_t>(v) * 1024 * 1024;
            else if (k == "CacheTtlSec")      g_cacheTtlMs = v * 1000ull;
            else if (k == "SweepIntervalSec") g_sweepIntervalMs = v * 1000ull;
            else if (k == "PrefetchThreads")  g_prefetchThreads = v < 1 ? 1u : (v > kPrefetchThreadsMax ? kPrefetchThreadsMax : static_cast<uint32_t>(v));
            else if (k == "PrefetchQueueCap") g_prefetchQueueCap = static_cast<size_t>(v);
        }
    }

    int Serve()
    {
        // One host per session.
        HANDLE singleton = CreateMutexA(nullptr, FALSE, "Local\\WraithHostSingleton");
        if (singleton && GetLastError() == ERROR_ALREADY_EXISTS)
        {
            wraith::core::log::Printf("host: another instance is running, exiting");
            return 0;
        }

        if (g_clientPid)
        {
            HANDLE client = OpenProcess(SYNCHRONIZE, FALSE, g_clientPid);
            if (client) CreateThread(nullptr, 0, ClientWatcher, client, 0, nullptr);
        }

        g_clientRoot = ClientRoot();

        // Mount the archive set up front (one archive-set open), then read the resolution tables
        // FROM the archives - never from a filesystem path.
        EnsureMounted(0);
        if (!g_db2.Load(*g_mpq[0]))
            wraith::core::log::Printf("host: WARNING resolve tables failed to load (FDID resolution disabled)");

        // Dependency prefetch pool: warms the byte cache with a served asset's direct deps so the client's
        // later synchronous opens hit the cache instead of stalling on a read + translate.
        for (uint32_t i = 0; i < g_prefetchThreads; ++i)
            CloseHandle(CreateThread(nullptr, 0, PrefetchWorker, reinterpret_cast<LPVOID>(static_cast<uintptr_t>(i)), 0, nullptr));

        if (!wraith::host::ipc::Create())
        {
            wraith::core::log::Printf("host: ShmServer.Create failed (err %lu)", GetLastError());
            return 1;
        }

        if (g_console)
            SetConsoleTitleA("WraithHost  -  client <-> host IPC");
        HOST_CONSOLE("WraithHost serving. Waiting for requests...\n\n");
        wraith::core::log::Printf("host: serving (%u channel, %u prefetch threads)", kChannels, g_prefetchThreads);

        // Single channel: the client serializes its opens, so serve them on THIS thread (the prefetch
        // pool is the parallel translation lane). No per-channel worker threads.
        Worker(reinterpret_cast<LPVOID>(static_cast<uintptr_t>(0)));
        return 0;
    }
}

int main(int argc, char** argv)
{
    g_dataDir = ExeDir();

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) g_dataDir = argv[++i];
        else if (a == "--client-pid" && i + 1 < argc) g_clientPid = static_cast<DWORD>(strtoul(argv[++i], nullptr, 10));
        else if (a == "--console") g_console = true;
    }

    wraith::core::log::Open((g_dataDir + "\\WraithHost.log").c_str());
    LoadConfig();
    int rc = Serve();
    wraith::core::log::Close();
    return rc;
}
