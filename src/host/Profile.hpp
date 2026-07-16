// Low-overhead request profiling for the 64-bit asset host.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace wxl::host::profile
{
    enum class RequestOp : uint8_t
    {
        Unknown,
        FileOpen,
        FileRead,
        FileClose,
        FileExists,
        Count
    };

    /** @brief Timing and outcome details accumulated while one file-open request is produced. */
    struct OpenTrace
    {
        uint64_t providerTicks = 0;
        uint64_t transformCacheTicks = 0;
        uint64_t archiveTicks = 0;
        uint64_t transformTicks = 0;
        uint64_t servedTicks = 0;
        uint64_t blobTicks = 0;

        uint64_t bytes = 0;
        uint64_t inlineBytes = 0;
        uint64_t sharedBytes = 0;

        uint32_t providerCalls = 0;
        uint32_t providerHits = 0;
        uint32_t transformCacheLookups = 0;
        uint32_t transformCacheHits = 0;
        uint32_t transformCacheStores = 0;
        uint32_t archiveReads = 0;
        uint32_t archiveMisses = 0;
        uint32_t transformCalls = 0;
        uint32_t transformClaims = 0;
        uint32_t servedCalls = 0;
        uint32_t aliasesTried = 0;
        uint32_t inlineResponses = 0;
        uint32_t blobCreates = 0;
        uint32_t blobReuses = 0;
        uint32_t blobFailures = 0;

        bool ok = false;
        bool miss = false;
    };

    /** @brief Request metadata handed from the decoder to the periodic profiler. */
    struct RequestTrace
    {
        RequestOp op = RequestOp::Unknown;
        std::string name;
        OpenTrace open;
        uint64_t bytes = 0;
        bool ok = false;
        bool badRequest = false;
    };

    /** @brief Live host memory gauges sampled only when a report is due. */
    struct Gauges
    {
        size_t transformCacheEntries = 0;
        uint64_t transformCacheBytes = 0;
        size_t blobs = 0;
        uint64_t blobBytes = 0;
        uint64_t blobRefs = 0;
    };

    /** @brief Returns a QueryPerformanceCounter timestamp, or zero when profiling is disabled. */
    uint64_t Now();

    /** @brief Records one completed host request. Called by the single serve thread. */
    void RecordRequest(const RequestTrace& trace, uint64_t serviceTicks, uint64_t postResponseTicks);

    /** @brief Reports whether the configured window elapsed; cheap enough to call after every request. */
    bool ReportDue();

    /** @brief Emits and resets the due report using gauges sampled off the client wait path. */
    void Report(const Gauges& gauges);

    /** @brief Logs the active profiling configuration at startup. */
    void LogSettings();
}
