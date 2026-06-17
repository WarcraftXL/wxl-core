// WMO multi-pass adapter: compose dropped layer textures (shine / emissive) over the native base draw.
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

#pragma once

#include <cstdint>

// DLL-only. The per-WMO consumer of the shared multipass mechanism. The host ships a per-material plan as a
// trailing WMP1 chunk; this module ingests it from the served bytes, then replays the dropped layer textures
// as extra additive passes after the native per-batch draw.
namespace wraith::runtime::wmo::multipass
{
    // Ingest a served WMO file: if it carries a trailing WMP1 plan chunk, register the plan keyed by the
    // file path and return the WMO-only length (the plan chunk hidden from the native loader). Returns the
    // original size unchanged when there is no plan.
    uint32_t IngestWmoBytes(const char* name, const uint8_t* buffer, uint32_t size);

    // Replay the registered extra passes for one group, after its native base draw. No-op when the group's
    // root has no plan. Self-guarded against faults.
    void RunExtraPasses(int groupObj);
}
