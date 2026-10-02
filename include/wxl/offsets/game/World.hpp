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
    // Tile fields: async read-in-flight handle (0 = idle), open file handle, raw ADT file-buffer ptr
    // (0 = not loaded), and that buffer's byte size (kOffTileFileId is a historical misnomer -- it
    // holds a byte count, not an id). Same tile-area object as ADT.hpp's TileArea struct, which is the
    // canonical typed (offset-checked) view of these fields -- use that from C++ code instead of these
    // raw constants when a named struct member will do.
    constexpr size_t kOffTileAsyncRead  = 0x70;
    constexpr size_t kOffTileFileHandle = 0x6C;
    constexpr size_t kOffTileFileBuffer = 0x80;
    constexpr size_t kOffTileFileId     = 0x84;
    // Per-tile ADT loader: formats <dir>\<name>_<x>_<y>.adt and queues the read. Native __cdecl(tile);
    // the tile index fields are at tile+0x48 (first %d) / tile+0x4c (second %d). Detoured for terrain phasing.
    using TileLoaderFn = void(__cdecl*)(void* tile);
    constexpr size_t kOffTileIdxFirst  = 0x48;
    constexpr size_t kOffTileIdxSecond = 0x4C;
    // Archive file-exists test. __stdcall taking two args (ret 8): (value, &pathObject), not a bare C
    // string. Resolves against the Client's own archive/loose set only, not files a client provider
    // or transform serves synthetically (StorageHook does not hook this entry point).
    constexpr uintptr_t kFileExists = 0x00422170;
    // Loader path globals: the bare map name and the "World\Maps\<Map>" dir string the per-tile loader
    // formats into <dir>\<name>_<x>_<y>.adt. The address holds the string buffer (passed by &).
    constexpr uintptr_t kMapNameStr = 0x00CE06D0;
    constexpr uintptr_t kMapDirStr  = 0x00CE07D0;
    // Terrain tile size (yards) and the grid origin (32 tiles * tile size); tile idx = (origin - world)/size.
    constexpr float kTileSizeYards = 533.33333f;
    constexpr float kGridOriginYards = 32.0f * kTileSizeYards;

    // --- view distance ---
    // Sets how far the world is drawn, clamping the value to what the loaded map allows -- so it reads
    // the current map id and belongs after a map load, not before. It also fixes the near plane at 0.2
    // and dirties the render pools.
    //
    // The scene culls against this and the horizon fades to fog over it: left unset, nothing survives
    // the cull and the frame is the fog colour alone. World__LoadMap sets it as part of loading a map;
    // kMapEnter does not, so a map entered that way needs it set explicitly.
    constexpr uintptr_t kSetFarClip = 0x00780800;
    using World_SetFarClipFn = void(__cdecl*)(float farClip);

    // The clamp SetFarClip calls internally (disasm-confirmed x87 clamp(farClip, 183.33333, ceiling)):
    // ceiling is 791.6667 or 1583.3334, picked by map id (old-continent ids stay at the lower tier
    // unless s_cvFarClipOverride's value is >= 1) and, off the old continents, by physical RAM (< ~1GB
    // -> lower tier). Both float(__cdecl) params/return -- the x87 return convention applies regardless
    // of the integer calling convention, so no special handling is needed on the C++ side.
    constexpr uintptr_t kValidateFarClip = 0x00780770;
    using World_ValidateFarClipFn = float(__cdecl*)(float farClip, int32_t mapId);

    // The live view distance and the one the previous frame used. The scene update compares them, and
    // treats a rise of more than 10 yards as a teleport: it raises the loading screen and puts the map
    // into preload, which drains the async queues every frame. Setting the distance leaves the two
    // differing by exactly the change, so a first-ever setting reads as a jump of its whole value --
    // and off the world nothing ever dismisses the loading screen it raises.
    constexpr uintptr_t kFarClip     = 0x00CD7748;
    constexpr uintptr_t kPrevFarClip = 0x00CD7744;

    // --- what the terrain pass consults before drawing ---
    // Per-category render switches. The solid terrain pass walks its list of visible chunks either way,
    // but only issues the draw when bit 1 is set -- so an unset bit renders nothing while looking, from
    // the outside, exactly like an empty list.
    constexpr uintptr_t kEnables         = 0x00CD774C;
    constexpr uint32_t  kEnableSolidTerrain = 0x02;
    // Head of the visible-chunk list the solid pass walks. Null, or an odd value standing for the list
    // sentinel, means nothing survived culling.
    constexpr uintptr_t kVisibleChunkHead = 0x00CDAF68;

    // --- per-frame scene update ---
    // Sets the area of interest, rebuilds the camera basis and the culling frustum, resolves whether
    // the viewer is indoors, and advances day/night. Takes an eye and a point it looks at -- a target,
    // not an orientation -- plus the point terrain streams around.
    //
    // The render culls against what this leaves behind and reads its viewer position from the globals
    // it writes, so a render without it culls against whatever was there before: for a world that was
    // never entered, a frustum that rejects everything and a viewer at the origin.
    constexpr uintptr_t kPrepareUpdate = 0x007831A0;
    using World_PrepareUpdateFn = void(__cdecl*)(const float* eye, const float* target,
                                                 const float* streamFocus);

    // --- scene draw ---
    // World scene render (viewerPos, flags): the terrain / WMO / doodad draw itself, a thin wrapper over
    // the world-scene renderer. viewerPos is a float[3]: the world frame passes the position field inside
    // its active camera, not a scene handle, and the world-scene renderer reaches its own state through
    // globals. So the
    // draw needs neither a world frame nor a world session -- only a position and the camera matrices.
    // flags is the world frame's own render-flag dword; 0 is the neutral value.
    constexpr uintptr_t kRender = 0x0077EFF0;
    using World_RenderFn = void(__cdecl*)(const float* viewerPos, uint32_t flags);

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
    // Active camera within a world frame. CGWorldFrame::GetActiveCamera (0x004F5960) returns it, but
    // other paths inline the same access straight off the global instead of calling it --
    // CGWorldFrame::GetCameraPosition (0x004F6650) is one, with six callers of its own. Substituting
    // the global therefore covers what detouring the accessor cannot.
    constexpr size_t kWorldFrameCamera = 0x7E20;
    // Fields the world render path touches reach 0xB18; a stand-in frame reserves well past that.
    constexpr size_t kWorldFrameStandInBytes = 0x10000;
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
    // Cursor hit test: casts the ray, returns the hit type (0 miss, 2 M2/doodad, 3 terrain/WMO), fills
    // result[6]. It is the cursor's entry, not a collision primitive -- it ends by projecting its
    // result to screen coordinates and setting the cursor depth, which reads a camera off the world
    // frame and faults wherever that is not up. For a plain ray against the world use kWorldIntersect
    // below; this one is for reproducing what the cursor does, including the model search.
    constexpr uintptr_t kIntersectWrapper = 0x004F9930;
    // Pick "flags" parameter for the wrapper: the value the engine's own cursor pick uses on a click. It
    // runs the terrain + WMO + M2-geometry intersect (the wrapper applies kPickMaskAnything internally).
    constexpr uint32_t kPickFlagsCursor = 1;
    // The mask the wrapper hands to the ray/world intersect: terrain and map objects. Model geometry
    // is not in it -- the wrapper tests models separately, through its own closest-model search.
    constexpr uint32_t kPickMaskAnything = 0x01000124;

    // Ray against the world, the primitive underneath the cursor hit test. The hit test is the wrong
    // entry for a plain collision query: it ends by projecting its result to screen coordinates and
    // updating the cursor depth, which faults wherever the frame it reads that from is not up. This
    // one only intersects.
    //
    // dist is in-out: 1.0 asks for the whole segment, and comes back as the fraction of it reached.
    // The hit point is that fraction along start->end, which is how the client's own callers derive it.
    constexpr uintptr_t kWorldIntersect = 0x0077F310;
    using WorldIntersectFn = char(__cdecl*)(const float* start, const float* end, void* outUnused,
                                            float* inOutDist, uint32_t mask, uint32_t flags);
    // Scratch hit-test coordinates populated by the world frame's default-action setup.
    // Do not use these as a live cursor source; they can retain an older action point.
    constexpr size_t kWorldFrameCursorDdcX = 0x310;
    constexpr size_t kWorldFrameCursorDdcY = 0x314;

    // Screen->ray (this = world frame): (sx, sy, &near, &far) -> nonzero if inside the viewport.
    using ScreenToRayFn = char(__thiscall*)(void* worldFrame, float sx, float sy, void* outNear, void* outFar);
    // Cursor pick wrapper: (rayStart, rayEnd, flags, result[6]) -> hit type (0/2/3).
    using IntersectFn = int(__cdecl*)(const void* rayStart, const void* rayEnd, uint32_t flags, void* result6);

    // --- async I/O queue primitives ---
    // Blocks pumping until ONE specific object's read completes. Reentrancy-guarded by a single global
    // counter.
    constexpr uintptr_t kAsyncFileReadWait = 0x004BA060;
    using AsyncFileReadWaitFn = void(__cdecl*)(void* asyncObj);
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

    // Cancel + recycle an in-flight async read object: spin-waits an
    // in-service read, unlinks a queued completion so it never runs, CLOSES the object's file handle
    // (+0x00), and recycles the node. Used to retire a tile's pending read before its buffer is freed.
    constexpr uintptr_t kAsyncDestroy = 0x004B9DE0;
    using AsyncDestroyFn = void(__cdecl*)(void* asyncObj);

    // Allocate (or recycle) one zero-initialized 0x30-byte async-read record. The caller fills
    // +0x00 file / +0x04 dest buffer / +0x08 size / +0x0C ctx / +0x10 completion (__cdecl, one arg =
    // ctx, invoked on the MAIN thread by the completion drain) and enqueues via kAsyncFileReadObject.
    constexpr uintptr_t kAsyncFileReadAllocObject = 0x004BA170;
    using AsyncFileReadAllocObjectFn = void*(__cdecl*)();
    // Async-read record field offsets (verified against the tile-area load routine 0x007D7150 and the drain).
    constexpr size_t kOffAsyncFile     = 0x00;
    constexpr size_t kOffAsyncBuffer   = 0x04;
    constexpr size_t kOffAsyncSize     = 0x08;
    constexpr size_t kOffAsyncCtx      = 0x0C;
    constexpr size_t kOffAsyncCallback = 0x10;

    // --- async disk-queue producer (multithreading) ---
    // Creates the non-streaming "Disk Queue" worker (1 in our context; native code supports up to 3,
    // gated by streaming mode). One caller (boot). Detoured to extend with 2 more queue/thread pairs.
    constexpr uintptr_t kAsyncFileReadInitialize = 0x004BAA40;
    using AsyncFileReadInitializeFn = void(__cdecl*)(uint32_t maxPerSecond, uint32_t pumpBudgetMs);
    // Enqueue: routes every read request to a queue. Native code always picks slot 0 (kAsyncQueueSlots)
    // outside streaming mode, regardless of how many worker threads exist. Detoured for round-robin.
    constexpr uintptr_t kAsyncFileReadObject = 0x004BAB50;
    using AsyncFileReadObjectFn = void(__cdecl*)(void* asyncObj, uint32_t highPriorityFlag);
    // Allocate one AsyncQueue (0x24 bytes), no args. Reused verbatim to add worker slots.
    constexpr uintptr_t kAsyncQueueAlloc = 0x004BA8E0;
    using AsyncQueueAllocFn = void*(__cdecl*)();
    // Wrap a queue + SThread__Create its worker thread, named from the 2nd arg. Reused verbatim.
    constexpr uintptr_t kAsyncThreadWrap = 0x004BA980;
    using AsyncThreadWrapFn = void(__cdecl*)(void* queue, const char* name);
    // Priority-sorted insert into a queue's pending list ("list A" / "list B", chosen by the queue's own
    // +0x20 flag inside AsyncFileReadObject; a queue Patch A creates never sets that flag, so it always
    // resolves to AsyncFileReadLinkObject).
    constexpr uintptr_t kAsyncFileReadLinkObject = 0x004BA3D0;
    using AsyncFileReadLinkObjectFn = void(__cdecl*)(void* asyncObj, uint32_t highPriorityFlag);
    // Same priority-sorted insert, "list B" -- chosen instead of kAsyncFileReadLinkObject only for a
    // queue whose own +0x20 flag is set (native code sets it only on the streaming-only 3rd slot; a
    // queue Patch A creates never sets it, so this is unreachable under Patch A/B, kept for parity).
    constexpr uintptr_t kAsyncFileReadLinkObjectAlt = 0x004BA530;
    // Splice a TS-list node to the head of its list. __thiscall: ECX = the list head (queue+0x8), one
    // stack arg = the node, callee-popped (ret 4). Used by AsyncFileReadObject's force-wait fast path.
    constexpr uintptr_t kTSListLinkToHead = 0x007B5020;
    using TSListLinkToHeadFn = void(__thiscall*)(void* listHead, void* node);
    // 3-slot AsyncQueue* array (stride 4): [0] = "Disk Queue" (always live), [1]/[2] = native streaming-
    // only slots, valid-but-null outside streaming mode. Patch A populates [1]/[2] itself. The dword
    // immediately after slot [2] is a real, already-used global (kOffAsyncQueueSleepThrottle-style
    // clamp target) -- never write index 3 or beyond, the array is proven exactly this size.
    constexpr uintptr_t kAsyncQueueSlots     = 0x00B4A20C;
    constexpr uint32_t  kAsyncQueueSlotCount = 3;

    // --- signatures ---
    // World tick + drain (param on stack).
    using World_TickFn = void(__cdecl*)(int param);
    // Async wait-all.
    using World_AsyncWaitAllFn = void(__cdecl*)();
    // Async pending predicate.
    using World_AsyncPendingFn = int(__cdecl*)();
    // Async service one pump (two args on stack).
    using World_AsyncServiceQueuesFn = void(__cdecl*)(int a, int b);

    // Area / zone resolution
    /// The indoor/outdoor verdict that gates fog, sky and outdoor lighting - overriding it is the
    /// cheapest way to force a lighting regime. __cdecl, caller-cleaned.
    constexpr uintptr_t kOutdoorsQuery                     = 0x0077FBF0;
    /// The top-level "which area am I in" resolver - a hook here can inject areas that do not exist in
    /// the client's AreaTable. __cdecl, caller-cleaned.
    constexpr uintptr_t kMapAreaIdQuery                    = 0x00782560;

    // Async file-read queues
    /// Registers a completion/status observer on the engine's own queue, which is cheaper and safer
    /// than detouring the poll handler. __cdecl, caller-cleaned.
    constexpr uintptr_t kAsyncStatusHandlerAdd             = 0x004B9D20;
    /// Cancellation is the one async path an extension must mirror or it will service a request whose
    /// owner is already gone - the missing piece next to the already-known submit/wait entries.
    /// __cdecl, caller-cleaned.
    constexpr uintptr_t kAsyncRequestCancel                = 0x004B9EA0;
    /// The point at which the IO threads go away - an extension holding outstanding requests must drain
    /// here or crash on exit. __cdecl, caller-cleaned.
    constexpr uintptr_t kAsyncShutdown                     = 0x004BAC50;

    // Camera and view
    /// The accessor six subsystems use for camera position - hooking it lets an extension present a
    /// virtual camera consistently everywhere at once. __cdecl, caller-cleaned.
    constexpr uintptr_t kFrameCameraPositionGet            = 0x004F6650;
    /// Retargets the camera at a world entity - the entry point for extension camera modes. __thiscall,
    /// 2 stack args.
    constexpr uintptr_t kFrameCameraTargetSet              = 0x004F6F50;

    // Cursor picking, ray intersection and collision queries
    /// The per-frame cursor-over-terrain resolution, giving an extension the world position under the
    /// mouse every frame without doing its own raycast. __thiscall, 1 stack arg.
    constexpr uintptr_t kFrameTerrainTrack                 = 0x004F66C0;
    /// Raw world-frame mouse input before any selection logic, so an extension can claim clicks for its
    /// own tools. __thiscall, 2 stack args.
    constexpr uintptr_t kFrameMouseDown                    = 0x004F6C10;
    /// The filter that decides whether a picked object is selectable - the hook for extensions that add
    /// selectable object classes. __stdcall, 2 stack args.
    constexpr uintptr_t kFrameSelectionLegalTest           = 0x004F7530;
    /// The click-to-interact dispatcher - an extension can add or veto default world-click actions.
    /// __thiscall, 1 stack arg.
    constexpr uintptr_t kFrameDefaultActionPerform         = 0x004F7880;
    /// The model-level resolution step of cursor picking - a hook here changes what the cursor can pick
    /// without replacing the whole hit test. __thiscall, 4 stack args.
    constexpr uintptr_t kFrameClosestModelFind             = 0x004F9550;
    /// The per-object callback of the hit-test enumeration - the finest-grained place to include or
    /// exclude candidates during picking. __cdecl, caller-cleaned.
    constexpr uintptr_t kFrameHitTestEnumProc              = 0x004F9F70;
    /// Maps a liquid volume to its ambient sound - needed to give a new liquid type audio without a
    /// LiquidType row. __cdecl, caller-cleaned.
    constexpr uintptr_t kMapLiquidSoundQuery               = 0x0079D2B0;
    /// Sits directly under the already-known World.Intersect, so an extension can change intersection
    /// behaviour without re-implementing the public wrapper's argument marshalling. __cdecl, caller-
    /// cleaned.
    constexpr uintptr_t kMapRayIntersect                   = 0x007A3B70;
    /// The collision-facet gather that backs the public facet API - the right level for an extension to
    /// add or filter collision surfaces. __cdecl, caller-cleaned.
    constexpr uintptr_t kMapFacetQuery                     = 0x007A5DD0;

    // Loading gate and loading screen
    /// The highest-fanout loading-gate release in the client (12 call sites) - the definitive "world is
    /// now visible" moment for an extension to resume work. __cdecl, caller-cleaned.
    constexpr uintptr_t kLoadingScreenDisable              = 0x00409550;
    /// The loading-screen raise, keyed by map - lets an extension supply its own screen art or hold the
    /// gate open while it streams. __cdecl, caller-cleaned.
    constexpr uintptr_t kLoadingScreenEnable               = 0x0040AB70;
    /// An extension can install its own load-progress sink (or wrap the engine's) to drive a custom
    /// loading UI or to know how far a map load has got. __cdecl, caller-cleaned.
    constexpr uintptr_t kLoadProgressCallbackSet           = 0x0077EC90;

    // Map lifecycle: load / unload / enter / leave
    /// The world subsystem's construction point - the earliest safe place to install world-scoped
    /// state, after the GX device exists but before any map is touched. __cdecl, caller-cleaned.
    constexpr uintptr_t kInitialize                        = 0x00780F50;
    /// Single choke point where a map id/name enters the engine - an extension can rewrite the target
    /// map, stage per-map assets, or fire a "map loading" event before any tile work starts. __cdecl,
    /// caller-cleaned.
    constexpr uintptr_t kMapLoadBegin                      = 0x00781430;
    /// The exact moment the current map is torn down, so an extension can flush its own per-map caches
    /// in lockstep with the engine. __cdecl, caller-cleaned.
    constexpr uintptr_t kMapUnload                         = 0x00783180;
    /// Symmetric teardown hook for anything installed at World.Initialize. __cdecl, caller-cleaned.
    constexpr uintptr_t kShutdown                          = 0x007837F0;
    /// Where the tile heaps and the tile grid are sized and built - the place to widen or instrument
    /// the map allocator pools. __cdecl, caller-cleaned.
    constexpr uintptr_t kMapInitialize                     = 0x0079E7C0;
    /// Pairs with Map.Initialize for heap teardown ordering. __cdecl, caller-cleaned.
    constexpr uintptr_t kMapDestroy                        = 0x0079F320;
    /// The pre-warm pass before the first frame of a map, so an extension can prime its own streaming
    /// ahead of the loading screen dropping. __cdecl, caller-cleaned.
    constexpr uintptr_t kMapPreload                        = 0x007BD9F0;
    /// The whole-map descriptor parse - where MPHD flags, the tile-present bitmap and global WMO
    /// records are decided, so a modern-format extension can substitute its own map table. __cdecl,
    /// caller-cleaned.
    constexpr uintptr_t kMapWdtParse                       = 0x007BF8B0;
    /// The map-scoped purge that runs between two maps - the correct place to drop per-map extension
    /// data without leaking across a zone change. __cdecl, caller-cleaned.
    constexpr uintptr_t kMapUnloadTiles                    = 0x007C3830;

    // Terrain tile streaming and the tile grid
    /// The map subsystem's own guarded file open - a modern-asset extension can redirect ADT/WMO paths
    /// here without touching the general IO layer used by everything else. __cdecl, caller-cleaned.
    constexpr uintptr_t kMapFileOpen                       = 0x007BD480;
    /// Pairs with Map.FileOpen so a substituted map file can be served entirely from extension memory.
    /// __cdecl, caller-cleaned.
    constexpr uintptr_t kMapFileRead                       = 0x007BD4D0;
    /// The tile-record allocator - the single place to enlarge, pool or instrument per-tile storage,
    /// and the natural counterpart to the already-known free at 0x007C00A0. __cdecl, caller-cleaned.
    constexpr uintptr_t kMapTileAlloc                      = 0x007C07C0;

    // World frame update chain
    /// The UI-side driver that decides what game time is handed to the day-night system - the place to
    /// override time of day without touching DayNight itself. __thiscall, 1 stack arg.
    constexpr uintptr_t kFrameDayNightUpdate               = 0x004F8410;
    /// A per-frame world-frame callback that already has the active player resolved - a cheap, safe
    /// attach point that does not sit inside a hot loop. __cdecl, caller-cleaned.
    constexpr uintptr_t kFramePerFrameTick                 = 0x004F8730;
    /// Per-object refresh from the world frame's side (nameplate/track/hit-test bookkeeping) -
    /// complements the known object update handler on the game-object side. __thiscall, 2 stack args.
    constexpr uintptr_t kFrameObjectUpdate                 = 0x004F8D10;
    /// The frame delta arrives here and fans out to camera update, day-night and terrain tracking - one
    /// detour to observe or rescale world time. __thiscall, 1 stack arg.
    constexpr uintptr_t kFrameLayerUpdate                  = 0x004FA040;
    /// The world frame's own update pass, sitting between UI frame time and world simulation - the
    /// natural attach point for extension logic that must run once per world frame with the frame's
    /// state already resolved. __thiscall, caller-cleaned.
    constexpr uintptr_t kFrameWorldUpdate                  = 0x004FA5F0;
    /// A very small wrapper that calls world update then world render - the cheapest single place to
    /// bracket the entire world frame from an extension. __thiscall, 2 stack args.
    constexpr uintptr_t kFrameFrameRender                  = 0x004FB080;

    // World object residency
    /// Guaranteed object-removal notification, so extension-side per-object state cannot outlive the
    /// object. __cdecl, caller-cleaned.
    constexpr uintptr_t kObjectDestroy                     = 0x00782680;

    // World render parameters and screen effects
    /// Selects the full-screen post effect (death/underwater grading) - the hook for an extension that
    /// wants to own screen grading. __cdecl, caller-cleaned.
    constexpr uintptr_t kFrameScreenEffectSet              = 0x004F7020;
    /// The per-frame publish of screen-effect parameters, 7 call sites deep in the render path - the
    /// place to blend in extension grading each frame. __cdecl, caller-cleaned.
    constexpr uintptr_t kFrameScreenEffectUpdate           = 0x004F88B0;
    /// Grass/detail draw distance with the engine's own clamp and squared-distance bookkeeping done for
    /// you - safer than writing the two globals directly. __cdecl, caller-cleaned.
    constexpr uintptr_t kDetailDoodadDistSet               = 0x00780730;
    /// The graphics-preset application - hooking it lets an extension define its own quality tiers
    /// rather than fighting the client's. __cdecl, caller-cleaned.
    constexpr uintptr_t kParamDefaultsApply                = 0x0078E1A0;
    /// Where every world-quality CVar and its change callback are registered - an extension can add its
    /// own world CVars into the same table, or capture the callback pointers. __cdecl, caller-cleaned.
    constexpr uintptr_t kParamInitialize                   = 0x0078E400;

    // World scene and view setup
    /// The engine's own NDC polygon clipper - reusable by an extension that needs the client's exact
    /// clip semantics for occlusion or picking work. __cdecl, caller-cleaned.
    constexpr uintptr_t kNdcClip                           = 0x00791380;

    // World tick and per-frame update
    /// The function that decides which region of the world is considered "interesting" this frame -
    /// detouring it lets an extension widen or bias residency without touching the tile loader.
    /// __cdecl, caller-cleaned.
    constexpr uintptr_t kAreaOfInterestPrepare             = 0x00780860;
    /// The world's own per-frame entry with the frame delta in hand - the natural place to drive
    /// extension simulation at world rate rather than at render rate. __cdecl, caller-cleaned.
    constexpr uintptr_t kUpdate                            = 0x007815C0;
    /// Per-entity re-link into the map's spatial grid - the place to observe or override where a world
    /// entity is considered to live. __cdecl, caller-cleaned.
    constexpr uintptr_t kMapEntityUpdate                   = 0x007A1BC0;
}
