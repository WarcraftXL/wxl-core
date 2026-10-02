// Engine small-block heap allocator / free addresses and their signatures.
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

// Engine heap allocator / free entries reached two ways: core code and every extension's SDK layer
// call them through game/Mem.hpp's typed wrappers; wxl-engine-reforged additionally includes this
// header directly to HOOK kAlloc itself (a hook needs the raw address + signature, not a call wrapper).
namespace wxl::offsets::engine::mem
{
    // Allocate a block: (size, fileName, line, flags) -> pointer (size is rounded up internally).
    constexpr uintptr_t kAlloc = 0x0076E540;
    // Free a block obtained from the allocator: (ptr, fileName, line, flags).
    constexpr uintptr_t kFree  = 0x0076E5A0;

    using Mem_AllocFn = void*(__stdcall*)(uint32_t size, const char* file, int line, uint32_t flags);
    using Mem_FreeFn  = void(__stdcall*)(void* ptr, const char* file, int line, uint32_t flags);

    // --- OOM-ladder purge primitives (wxl-engine-reforged) ---------------------------------------
    // Neither of these reaches a currently-bound/in-use resource -- both only reclaim what the game
    // itself already marked idle (an unreferenced cached M2, an already-unbound recycled texture).
    // Real, safe recovery levers, just bounded: they help most right after an unload-heavy moment
    // (zone change), not guaranteed to free anything under sustained pressure from live content.

    // The model cache's garbage-collect entry (this, forceAll): forceAll!=0 evicts every
    // already-unreferenced cached model immediately (the routine per-tick call passes 0, which only
    // evicts models idle >9999ms). No lock, no reentrancy guard -- main thread only, and never from
    // inside the model cache/scene's own code.
    constexpr uintptr_t kM2CacheGarbageCollect = 0x0081C290;
    using M2Cache_GarbageCollectFn = void(__thiscall*)(void* cache, uint32_t forceAll);

    // The live model-cache pointer isn't reachable from a fixed global -- it lives on the model
    // scene's own instance, at kOffM2SceneCache. wxl-engine-reforged captures it opportunistically via
    // a lightweight hook on the model scene's own per-frame update (below), which runs every frame on
    // the real scene instance; see OomLadder.cpp. __fastcall with a dummy edx param is this codebase's
    // established shape for hooking a __thiscall target (matches e.g. AdtSplit's TileAreaLoadFn) -- the
    // real ORIGINAL pointer is still called thiscall-correct through this same typedef, the dummy param
    // only matters for the detour function's own signature.
    constexpr uintptr_t kM2SceneAdvanceTime = 0x0081C9C0;
    using M2Scene_AdvanceTimeFn = void(__fastcall*)(void* scene, void* edx, uint32_t deltaMs);
    constexpr size_t    kOffM2SceneCache    = 0x04;

    // The texture cache update: already runs every frame (its own per-frame event registration,
    // untouched here). Reentrant-safe by construction (only walks its own recycle-pool buckets) but
    // calls the renderer's texture-destroy routine directly -- main/render thread only, no args.
    constexpr uintptr_t kTextureCacheUpdate = 0x004B6AE0;
    using TextureCache_UpdateFn = void(__cdecl*)();
}
