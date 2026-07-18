// Terrain-phase loader detour: redirects loaded ADT tiles to a phase-variant map directory.
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

#include "config.hpp"
#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"

#include "common/Log.hpp"
#include "game/world/Loading.hpp"
#include "offsets/engine/Io.hpp"
#include "offsets/game/World.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
    namespace woff = wxl::offsets::game::world;
    namespace io   = wxl::offsets::engine::io;

    woff::TileLoaderFn g_origTileLoad = nullptr;
    char    g_childMap[64] = { 0 };       // active phase directory
    char    g_baseMap[64]  = { 0 };       // the base map the phase overlays
    uint8_t g_phaseMain[0x8000] = { 0 };  // phase WDT MAIN: 64x64 entries, 8 bytes each, bit0 = tile present

    uint32_t Rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

    /** @brief Bounded copy of a map-name string into a 64-byte buffer. */
    void CopyName(char* dst, const char* src)
    {
        std::size_t n = 0;
        for (; src && src[n] && n + 1 < 64; ++n) dst[n] = src[n];
        dst[n] = '\0';
    }

    /**
     * @brief Reads the phase map's WDT MAIN present table into g_phaseMain.
     *
     * Goes through the client's own storage entry points (which are detoured): a host-served phase map
     * and a native-archive one both resolve here, so the phase source's location never matters.
     */
    bool LoadPhaseMain(const char* dir)
    {
        std::memset(g_phaseMain, 0, sizeof g_phaseMain);
        char path[260];
        std::snprintf(path, sizeof path, "World\\Maps\\%s\\%s.wdt", dir, dir);

        auto open  = reinterpret_cast<io::Storage_FileOpenFn>(io::kFileOpen);
        auto size  = reinterpret_cast<io::Storage_FileSizeFn>(io::kFileSize);
        auto read  = reinterpret_cast<io::Storage_FileReadFn>(io::kFileRead);
        auto close = reinterpret_cast<io::Storage_FileCloseFn>(io::kFileClose);

        void* handle = nullptr;
        if (!open(nullptr, path, 0, &handle) || !handle)
        {
            WLOG_INFO("phasing: phase WDT '%s' not found", path);
            return false;
        }

        std::vector<uint8_t> wdt(size(handle, nullptr));
        uint32_t got = 0;
        if (!wdt.empty()) read(handle, wdt.data(), static_cast<uint32_t>(wdt.size()), &got, nullptr, 0);
        close(handle);
        wdt.resize(got);

        // Walk the WDT chunks for MAIN (stored 'NIAM'); copy its 64x64 present-table data into g_phaseMain.
        for (size_t p = 0; p + 8 <= wdt.size(); )
        {
            const uint32_t sz = Rd32(&wdt[p + 4]);
            if (std::memcmp(&wdt[p], "NIAM", 4) == 0)
            {
                const size_t n = sz < sizeof g_phaseMain ? sz : sizeof g_phaseMain;
                if (p + 8 + n <= wdt.size()) std::memcpy(g_phaseMain, &wdt[p + 8], n);
                return true;
            }
            p += 8 + sz;
        }
        WLOG_INFO("phasing: phase WDT '%s' has no MAIN chunk", path);
        return false;
    }

    /** @brief Reports whether the active phase has a variant tile at this tile's coords. */
    bool PhaseHasTile(const void* tile)
    {
        const int first  = *reinterpret_cast<const int*>(static_cast<const char*>(tile) + woff::kOffTileIdxFirst);
        const int second = *reinterpret_cast<const int*>(static_cast<const char*>(tile) + woff::kOffTileIdxSecond);
        if (first < 0 || first >= 64 || second < 0 || second >= 64) return false;
        const int slot = second * 64 + first; // matches DAT_00ce88d0 indexing (tile[0x4c]*64 + tile[0x48])
        return (g_phaseMain[slot * 8] & 1) != 0;
    }

    /**
     * @brief Per-tile loader detour: when a phase is active and it has this tile, serve the tile from the
     *        phase directory by swapping the loader path globals for this one load, then restoring them.
     * @param tile  the tile object being loaded (its x/y are at tile+0x48 / tile+0x4c).
     */
    void __cdecl hkTileLoad(void* tile)
    {
        if (!g_childMap[0] || !PhaseHasTile(tile)) { g_origTileLoad(tile); return; }

        char saveDir[0x40], saveName[0x40];
        std::memcpy(saveDir,  reinterpret_cast<void*>(woff::kMapDirStr),  sizeof saveDir);
        std::memcpy(saveName, reinterpret_cast<void*>(woff::kMapNameStr), sizeof saveName);
        std::snprintf(reinterpret_cast<char*>(woff::kMapDirStr),  sizeof saveDir,  "World\\Maps\\%s", g_childMap);
        std::snprintf(reinterpret_cast<char*>(woff::kMapNameStr), sizeof saveName, "%s", g_childMap);
        g_origTileLoad(tile);
        std::memcpy(reinterpret_cast<void*>(woff::kMapDirStr),  saveDir,  sizeof saveDir);
        std::memcpy(reinterpret_cast<void*>(woff::kMapNameStr), saveName, sizeof saveName);
    }

    bool InstallPhasing()
    {
        wxl::hook::Install("TileLoader", woff::kTileLoader, &hkTileLoad, &g_origTileLoad);
        return true;
    }
}

namespace wxl::game::world
{
    void SetTerrainPhase(const char* childMapDir)
    {
        if (!childMapDir || !childMapDir[0]) return;
        if (!LoadPhaseMain(childMapDir)) return; // need the phase present table to gate the per-tile redirect
        if (!g_baseMap[0]) CopyName(g_baseMap, MapName()); // remember the base map once
        CopyName(g_childMap, childMapDir);

        // Stay on the base map (its present table keeps every base tile); re-stream it - the loader detour
        // swaps in the phase ADT only for the tiles the phase actually has, leaving the rest as base.
        EnterMap(g_baseMap, MapId());
        WLOG_INFO("phasing: phase '%s' active over base '%s'", g_childMap, g_baseMap);
    }

    void ClearTerrainPhase()
    {
        if (!g_baseMap[0]) return; // no phase active
        g_childMap[0] = '\0';      // redirect off before the re-stream
        EnterMap(g_baseMap, MapId());
        WLOG_INFO("phasing: restored base map '%s'", g_baseMap);
        g_baseMap[0] = '\0';
    }
}

WXL_REGISTER_FEATURE("phasing", wxl::features::kPhasing, InstallPhasing)
