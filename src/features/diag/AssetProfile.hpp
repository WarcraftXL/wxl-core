// Coarse client-side asset-load and frame-hitch profiling.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include <cstdint>

namespace wxl::runtime::assetprof
{
    enum class Phase : uint8_t
    {
        M2Pre,
        M2Native,
        M2Post,
        TextureRequest,
        TextureUpload,
        WmoRootPre,
        WmoRootNative,
        WmoGroupPre,
        WmoGroupNative,
        Count
    };

    /** @brief Returns a QPC timestamp, or zero when client asset profiling is disabled. */
    uint64_t Now();

    /** @brief Adds one completed phase duration and optional work-unit count (texture-upload pixels). */
    void Record(Phase phase, uint64_t ticks, uint64_t units = 0);

    /** @brief Adds one engine frame delta and emits a due periodic summary outside the aggregation lock. */
    void RecordFrame(float deltaSeconds);
}
