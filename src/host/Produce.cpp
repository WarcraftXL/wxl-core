// Serve-side byte production: transform cache, name aliases, archive read and transform pipeline.
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

#include "Produce.hpp"

#include "Aliases.hpp"
#include "Host.hpp"
#include "Warm.hpp"
#include "common/Config.hpp"
#include "common/Log.hpp"

#include <cctype>
#include <list>
#include <mutex>
#include <unordered_map>

namespace hprof = wxl::host::profile;

namespace wxl::host::produce
{
    namespace
    {
        // Serve store, shared by every channel worker. MpqStore locks per archive internally, so two
        // workers only serialize when they read from the same archive; loose-folder reads and Transform
        // calls run fully concurrent across the pool.
        std::unique_ptr<wxl::host::mpq::MpqStore> g_mpq;

        // Transforms are pure for a given archive path. Cache transformed outputs in the 64-bit host so
        // expensive cold work like DXT component palettization and MD21 dechunk/downport is not repeated on
        // every reopen. Bytes are shared: an alias key and its resolved key point at one buffer, and a hit
        // copies the bytes out AFTER releasing the cache lock so a multi-MB hit never stalls the workers.
        struct TransformCacheEntry
        {
            std::shared_ptr<const std::vector<uint8_t>> bytes;
            std::list<std::string>::iterator lru;
        };

        // g_transformCacheMutex protects g_transformCache, g_transformCacheLru and g_transformCacheBytes.
        std::mutex g_transformCacheMutex;
        std::list<std::string> g_transformCacheLru; // least recently used at the front
        std::unordered_map<std::string, TransformCacheEntry> g_transformCache;
        size_t g_transformCacheBytes = 0;

        // --provide dumps must resolve native content too; see ForceServeNative().
        bool g_serveNativeForced = false;

        std::shared_ptr<const std::vector<uint8_t>> LookupTransformCache(const std::string& name,
                                                                         hprof::OpenTrace& trace)
        {
            ++trace.transformCacheLookups;
            const uint64_t started = hprof::Now();
            std::shared_ptr<const std::vector<uint8_t>> hit;
            {
                std::lock_guard<std::mutex> lock(g_transformCacheMutex);
                auto it = g_transformCache.find(name);
                if (it != g_transformCache.end())
                {
                    // Keep assets used by the current world resident. Without eviction the cache became a
                    // write-once 128 MB bucket: after it filled, every transform for a newly teleported-to
                    // area was repeated until the host restarted.
                    g_transformCacheLru.splice(g_transformCacheLru.end(), g_transformCacheLru, it->second.lru);
                    hit = it->second.bytes;
                }
            }
            trace.transformCacheTicks += hprof::Now() - started;
            if (hit) ++trace.transformCacheHits;
            return hit;
        }

        void RememberTransform(const std::string& name,
                               const std::shared_ptr<const std::vector<uint8_t>>& bytes,
                               hprof::OpenTrace& trace)
        {
            if (!bytes || bytes->empty() || bytes->size() > TransformCacheMaxEntry()) return;

            const uint64_t started = hprof::Now();
            std::lock_guard<std::mutex> lock(g_transformCacheMutex);
            if (g_transformCache.find(name) == g_transformCache.end())
            {
                auto pendingLru = g_transformCacheLru.end();
                try
                {
                    const size_t limit = TransformCacheMaxBytes();
                    while (!g_transformCacheLru.empty() && g_transformCacheBytes + bytes->size() > limit)
                    {
                        auto oldest = g_transformCache.find(g_transformCacheLru.front());
                        if (oldest != g_transformCache.end())
                        {
                            g_transformCacheBytes -= oldest->second.bytes->size();
                            g_transformCache.erase(oldest);
                        }
                        g_transformCacheLru.pop_front();
                    }

                    pendingLru = g_transformCacheLru.insert(g_transformCacheLru.end(), name);
                    auto [it, inserted] = g_transformCache.emplace(name, TransformCacheEntry{ bytes, pendingLru });
                    (void)it;
                    if (inserted)
                    {
                        g_transformCacheBytes += bytes->size();
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
                        WLOG_INFO("host: transform cache allocation failed; continuing uncached");
                    }
                }
            }
            trace.transformCacheTicks += hprof::Now() - started;
        }
    }

    void SetMpqStore(std::unique_ptr<wxl::host::mpq::MpqStore> store)
    {
        g_mpq = std::move(store);
    }

    bool ArchiveExists(std::string_view name)
    {
        return g_mpq && g_mpq->Exists(name);
    }

    void ForceServeNative()
    {
        g_serveNativeForced = true;
    }

    // When off (default), a request whose winning source is a standard stock archive answers NotFound
    // without reading a byte: the client mounts those same archives natively and reads the identical bytes
    // itself, off the IPC path. Loose folders, custom patch archives, aliases, providers and transforms
    // still serve. Set WXL_HOST_SERVE_NATIVE=1 to restore serving everything.
    bool ServeNativeArchives()
    {
        static const bool enabled = wxl::config::Env("WXL_HOST_SERVE_NATIVE", false);
        return enabled || g_serveNativeForced;
    }

    size_t TransformCacheMaxBytes()
    {
        static const size_t bytes = static_cast<size_t>(
            wxl::config::U64("WXL_TRANSFORM_CACHE_MB", 512, 32, 8192) * 1024ull * 1024ull);
        return bytes;
    }

    // The entry cap must hold a merged monolithic terrain tile (tens of MB), else every reopen of a big
    // tile repeats the whole merge -- the single most expensive host stall while streaming the world.
    size_t TransformCacheMaxEntry()
    {
        static const size_t bytes = static_cast<size_t>(
            wxl::config::U64("WXL_TRANSFORM_CACHE_ENTRY_MB", 64, 1, 512) * 1024ull * 1024ull);
        return bytes;
    }

    std::string NameKey(std::string_view name)
    {
        std::string key(name);
        for (char& c : key)
            c = (c == '/') ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return key;
    }

    bool ProduceCandidate(const std::string& requestName, const std::string& readName,
                          std::vector<uint8_t>& out, hprof::OpenTrace& trace, bool& nativeHit)
    {
        if (auto hit = LookupTransformCache(readName, trace))
        {
            if (readName != requestName) RememberTransform(requestName, hit, trace);
            out = *hit; // copy outside the cache lock
            return true;
        }

        // One fused walk: locate and read in the same pass, skipping the byte read entirely on a
        // Standard-archive hit that the client will re-read natively anyway.
        const bool skipStandard = !ServeNativeArchives() && readName == requestName;
        std::vector<uint8_t> raw;
        ++trace.archiveReads;
        const uint64_t archiveStarted = hprof::Now();
        const wxl::host::mpq::Source source = g_mpq->LocateAndRead(readName, !skipStandard, raw);
        trace.archiveTicks += hprof::Now() - archiveStarted;
        if (source == wxl::host::mpq::Source::None)
        {
            ++trace.archiveMisses;
            return false;
        }
        if (skipStandard && source == wxl::host::mpq::Source::Standard)
        {
            ++trace.nativeSkips;
            nativeHit = true;
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
            out = reshaped; // serve copy; the cache owns the moved-in original
            auto shared = std::make_shared<const std::vector<uint8_t>>(std::move(reshaped));
            RememberTransform(readName, shared, trace);
            if (readName != requestName) RememberTransform(requestName, shared, trace);
            return true;
        }
        out = std::move(raw);
        return true;
    }

    bool ProduceServed(const std::string& name, std::vector<uint8_t>& out, hprof::OpenTrace& trace)
    {
        bool nativeHit = false;
        if (ProduceCandidate(name, name, out, trace, nativeHit))
        {
            wxl::host::warm::QueueNeighborTiles(name);
            return true;
        }

        // A native hit means the client will read these exact bytes from its own archives; trying the
        // aliases here could serve a DIFFERENT file over a name the client resolves fine natively.
        if (nativeHit)
            return false;

        std::vector<std::string> aliases;
        BuildAliases(name, aliases);
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
            if (ProduceCandidate(name, alias, out, trace, nativeHit))
                return true;
        }

        return false;
    }

    void SnapshotGauges(size_t& entries, uint64_t& bytes)
    {
        std::lock_guard<std::mutex> lock(g_transformCacheMutex);
        entries = g_transformCache.size();
        bytes = g_transformCacheBytes;
    }
}
