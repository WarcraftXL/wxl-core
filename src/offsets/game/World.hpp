// World tick / load-gate entries, async-I/O queue primitives, and the load-state globals.
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

#include <cstdint>
#include <cstddef>

// INTERNAL to the core. World tick / load-gate entries, async-I/O queue primitives, and the
// load-state globals. Modules never include this; they use wxl::game / wxl::events.
namespace wxl::offsets::game::world
{
    // --- world tick / load gate ---
    // World tick + loading-screen synchronous drain (param): while the load-gate flag is set this runs
    // the drain that blocks dismissal until pending I/O and near-player objects are resident.
    constexpr uintptr_t kTick = 0x007B6B00;
    // Load-gate flag (u32): nonzero while the blocking drain runs.
    constexpr uintptr_t kLoadActive = 0x00ADFBC8;
    // CWorld::Enter(time, withLoadingScreen): unloads the old world and loads the new one (calls the
    // blocking load), then dismisses the loading screen. At entry the old world is still intact (leave
    // point); after it returns the new world + objects are resident (enter point).
    constexpr uintptr_t kEnter = 0x00781500;
    using World_EnterFn = void(__cdecl*)(int worldTime, int withLoadingScreen);
    // Focus world position floats X/Y/Z (the center of the load box / player position).
    constexpr uintptr_t kFocusPosX = 0x00CD7778;
    constexpr uintptr_t kFocusPosY = 0x00CD777C;
    constexpr uintptr_t kFocusPosZ = 0x00CD7780;

    // --- terrain re-stream (force a reload of the loaded ADT tiles) ---
    // World-tick teleport/purge flag: set nonzero to make the next world tick destroy every loaded tile
    // (the WDT-present grid is kept) and then re-stream the in-window tiles. Tile-owned objects
    // (doodads/WMO) re-spawn. Set on the main thread between frames.
    constexpr uintptr_t kPurgeReloadFlag = 0x00CD767C;
    // Streaming landmarks (addresses only): the per-frame streaming tick, the all-tiles purge, the tile
    // factory and the per-tile loader that builds <Map>_<x>_<y>.adt and queues the async read.
    constexpr uintptr_t kStreamingTick = 0x007B5950;
    constexpr uintptr_t kPurgeAllTiles = 0x007C3730;
    constexpr uintptr_t kTileFactory   = 0x007D9A70;
    constexpr uintptr_t kTileLoader    = 0x007D9A20;
    // Purge every loaded tile (unlink + unload + destroy each, free the secondary array). No args.
    using World_PurgeAllTilesFn = void(__cdecl*)();
    // Single-tile unload (clears the grid slot, destroys the tile) and the tile destructor (unlinks its
    // node, runs the destructor, frees it).
    constexpr uintptr_t kTileUnload  = 0x007C3700;
    constexpr uintptr_t kTileDestroy = 0x007C00A0;
    using TileDestroyFn = void(__cdecl*)(void* tile);
    // Async read-completion callback: finalizes the tile, frees the read context, clears the read handle.
    constexpr uintptr_t kReadComplete = 0x007D7020;
    using ReadCompleteFn = void(__cdecl*)(void* tile);

    // Active-tiles list (intrusive TS-list): the head holds the first node value (sentinel = bit0 set);
    // the link base holds the relative-pointer origin. Per node: tile = *(node+4); next =
    // *(*(u32*)kActiveListLinkBase + 4 + node). Tile grid: 64x64 Tile*, slot = tile[0x4c]*64 + tile[0x48].
    constexpr uintptr_t kActiveListHead     = 0x00ADFBF4;
    constexpr uintptr_t kActiveListLinkBase = 0x00ADFBEC;
    constexpr uintptr_t kTileGrid           = 0x00CE48D0;
    // Tile fields: async read-in-flight handle (0 = idle), ADT file-buffer ptr (0 = not loaded), file id.
    constexpr size_t kOffTileAsyncRead  = 0x70;
    constexpr size_t kOffTileFileBuffer = 0x80;
    constexpr size_t kOffTileFileId     = 0x84;
    // Per-tile ADT loader: formats <dir>\<name>_<x>_<y>.adt and queues the read. Native __cdecl(tile);
    // the tile index fields are at tile+0x48 (first %d) / tile+0x4c (second %d). Detoured for terrain phasing.
    using TileLoaderFn = void(__cdecl*)(void* tile);
    constexpr size_t kOffTileIdxFirst  = 0x48;
    constexpr size_t kOffTileIdxSecond = 0x4C;
    // Archive file-exists test. __stdcall taking two args (ret 8): (value, &pathObject), not a bare C
    // string. Resolves against the Client's own archive/loose set only, not host-served files.
    constexpr uintptr_t kFileExists = 0x00422170;
    // Loader path globals: the bare map name and the "World\Maps\<Map>" dir string the per-tile loader
    // formats into <dir>\<name>_<x>_<y>.adt. The address holds the string buffer (passed by &).
    constexpr uintptr_t kMapNameStr = 0x00CE06D0;
    constexpr uintptr_t kMapDirStr  = 0x00CE07D0;
    // Terrain tile size (yards) and the grid origin (32 tiles * tile size); tile idx = (origin - world)/size.
    constexpr float kTileSizeYards = 533.33333f;
    constexpr float kGridOriginYards = 32.0f * kTileSizeYards;

    // --- current map ---
    // Numeric map id of the loaded world (int32; -1 while none). The map loader writes it before
    // CWorld::Enter returns, so it is valid at the enter hook and still valid at the leave hook.
    constexpr uintptr_t kCurrentMapId = 0x00ADFBC4;

    // Map change: repoints the dir/name/wdt-path globals to <mapDir>, purges the loaded tiles, loads that
    // map's WDT (present table) + WDL, re-streams around the current camera, and drains the pending reads.
    // The player is not moved. mapId is written to kCurrentMapId.
    constexpr uintptr_t kMapEnter = 0x007BFCE0;
    using World_MapEnterFn = void(__cdecl*)(const char* mapDir, int mapId);

    // --- cursor world pick ---
    // CWorldFrame singleton holder: *(void**)kWorldFrame is the world frame (pass as the this/ECX).
    constexpr uintptr_t kWorldFrame = 0x00B7436C;
    // Native world->screen projection used by world text, chat bubbles and target indicators.
    // Fastcall: ECX = world frame, EDX is unused; worldPos/outScreen are float[3].
    // Returns nonzero when on-screen.
    constexpr uintptr_t kGetScreenCoordinates = 0x004F6D20;
    using GetScreenCoordinatesFn = int(__fastcall*)(void* worldFrame, void* unusedEdx,
                                                    const float* worldPos, float* outScreen,
                                                    uint32_t* clipFlags);
    // UI coordinate multipliers used by Blizzard's world-space projection conversion.
    constexpr uintptr_t kUiTexCoordAlphaMultiplier1 = 0x00AC0CB4;
    constexpr uintptr_t kUiTexCoordAlphaMultiplier3 = 0x00AC0CBC;
    // CGWorldFrame::SetupDefaultAction refreshes its hit-test point from the active input
    // object's normalized cursor immediately before calling HitTestPoint.
    constexpr size_t kWorldFrameInput = 0x00A0;
    constexpr size_t kInputCursorNdcX = 0x1224;
    constexpr size_t kInputCursorNdcY = 0x1228;
    constexpr uintptr_t kDdcWidth  = 0x00AC0CB4;
    constexpr uintptr_t kDdcHeight = 0x00AC0CB8;
    // Full cursor pick: sets up the world projection, builds the ray, and intersects, in one call. This is the engine's own per-frame mouseover entry
    // this = world frame; result[0..5] = {objLo, objHi, posX, posY, posZ, t}; returns the hit type.
    constexpr uintptr_t kPickAtScreen = 0x004F9DA0;
    using PickAtScreenFn = int(__thiscall*)(void* worldFrame, float ddcX, float ddcY, int mode, void* result12);
    // Mode the per-frame mouseover pick uses (the safe, always-exercised path).
    constexpr int kPickModeCursor = 0;

    // Lower-level pieces the full pick uses internally; documented landmarks.
    // Screen (DDC pixels) -> world ray: fills near/far points, returns nonzero when inside the viewport.
    constexpr uintptr_t kScreenToRay = 0x004F6450;
    // Cursor pick: casts the ray, returns the hit type (0 miss, 2 M2/doodad, 3 terrain/WMO), fills result[6].
    constexpr uintptr_t kIntersectWrapper = 0x004F9930;
    // Pick "flags" parameter for the wrapper: the value the engine's own cursor pick uses on a click. It
    // runs the terrain + WMO + M2-geometry intersect (the wrapper applies kPickMaskAnything internally).
    constexpr uint32_t kPickFlagsCursor = 1;
    // The CWorld::Intersect mask the wrapper applies internally (terrain + WMO + M2/doodad geometry).
    constexpr uint32_t kPickMaskAnything = 0x01000124;
    // Scratch hit-test coordinates populated by CGWorldFrame::SetupDefaultAction.
    // Do not use these as a live cursor source; they can retain an older action point.
    constexpr size_t kWorldFrameCursorDdcX = 0x310;
    constexpr size_t kWorldFrameCursorDdcY = 0x314;

    // Screen->ray (this = world frame): (sx, sy, &near, &far) -> nonzero if inside the viewport.
    using ScreenToRayFn = char(__thiscall*)(void* worldFrame, float sx, float sy, void* outNear, void* outFar);
    // Cursor pick wrapper: (rayStart, rayEnd, flags, result[6]) -> hit type (0/2/3).
    using IntersectFn = int(__cdecl*)(const void* rayStart, const void* rayEnd, uint32_t flags, void* result6);

    // --- async I/O queue primitives ---
    // Wait-all: blocks pumping the async queues until no async file read is pending.
    constexpr uintptr_t kAsyncWaitAll = 0x004BAE10;
    // Pending predicate: nonzero while any async file request still has outstanding work.
    constexpr uintptr_t kAsyncPending = 0x004BAD80;
    // Service the async queues one pump (called as (0, 0)). Re-entered synchronously while a texture
    // build force-waits a nested load, which is what exposes the singleton mip-table clobber.
    constexpr uintptr_t kAsyncServiceQueues = 0x004B9B20;
    // Takes two args (the engine calls it as (0, 0)); matches World_AsyncServiceQueuesFn below. The detour
    // must forward them, else the original runs with garbage a/b read off the stack.
    using AsyncServiceQueuesFn = int(__cdecl*)(int a, int b);

    // Cancel + recycle an in-flight async read object: unlinks it from the completed queue so its
    // completion never runs. Used to retire a chunk's pending read before its buffer is freed.
    constexpr uintptr_t kAsyncDestroy = 0x004B9DE0;
    using AsyncDestroyFn = void(__cdecl*)(void* asyncObj);

    // --- signatures ---
    // World tick + drain (param on stack).
    using World_TickFn = void(__cdecl*)(int param);
    // Async wait-all.
    using World_AsyncWaitAllFn = void(__cdecl*)();
    // Async pending predicate.
    using World_AsyncPendingFn = int(__cdecl*)();
    // Async service one pump (two args on stack).
    using World_AsyncServiceQueuesFn = void(__cdecl*)(int a, int b);
}
