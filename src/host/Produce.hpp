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

#pragma once

#include "Profile.hpp"
#include "mpq/MpqStore.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Turns a requested name into served bytes: consult the transform cache, else read the winning archive
// source once and offer the raw bytes to the transform hooks, expanding to the client's name aliases when
// a direct read misses. This is where the expensive cold work (DXT palettization, MD21 dechunk, terrain
// tile merges) is produced and cached; the serve loop only encodes the result into a response.
namespace wxl::host::produce
{
    /** @brief Installs the mounted archive store the produce pipeline reads from. */
    void SetMpqStore(std::unique_ptr<wxl::host::mpq::MpqStore> store);

    /**
     * @brief Reports whether the mounted archive set has `name` (the OpFileExists fast path).
     * @param name  file name queried
     * @return true if any mounted archive or loose root holds the file
     */
    bool ArchiveExists(std::string_view name);

    /**
     * @brief Forces the pipeline to serve stock-archive content instead of deferring it to the client.
     *        Used by the offline --provide dump, which must resolve native content too.
     */
    void ForceServeNative();

    /** @brief Reports whether stock (Standard) archive hits are served rather than left to the client. */
    bool ServeNativeArchives();

    /** @brief Returns the transform cache total-byte budget. */
    size_t TransformCacheMaxBytes();

    /** @brief Returns the per-entry transform cache size cap. */
    size_t TransformCacheMaxEntry();

    /** @brief Lowercases and swaps '/' for '\\' so two spellings of the same path compare equal. */
    std::string NameKey(std::string_view name);

    /**
     * @brief Reads raw archive bytes for `readName`, offers them to the transform hooks, else passes them through.
     *
     * A direct request (readName == requestName) whose winning source is a standard stock archive is not
     * read at all: `nativeHit` is set and the caller answers NotFound, letting the client read the same
     * bytes from its own natively mounted archive without the IPC copy. Alias reads always serve, since the
     * client cannot resolve an alias natively.
     * @param requestName  original requested name (cache key for a resolved alias)
     * @param readName     archive-internal file name actually read
     * @param out          receives the reshaped or raw bytes
     * @param trace        per-open counters and stage timings
     * @param nativeHit    set when the name was skipped in favour of the client's native read
     * @return false on an archive miss or a native skip; provider hooks are fired by the caller, not here
     */
    bool ProduceCandidate(const std::string& requestName, const std::string& readName,
                          std::vector<uint8_t>& out, profile::OpenTrace& trace, bool& nativeHit);

    /**
     * @brief Produces the bytes for a client open: direct candidate first, then the name aliases.
     * @param name   requested file name
     * @param out    receives the served bytes
     * @param trace  per-open counters and stage timings
     * @return true if the file (or one of its aliases) produced bytes
     */
    bool ProduceServed(const std::string& name, std::vector<uint8_t>& out, profile::OpenTrace& trace);

    /**
     * @brief Samples transform-cache residency for a periodic profile report.
     * @param entries  receives the number of cached entries
     * @param bytes    receives the resident transform-cache bytes
     */
    void SnapshotGauges(size_t& entries, uint64_t& bytes);
}
