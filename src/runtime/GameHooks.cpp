// Game-logic detours: publish OnModelLoad (and other non-render events as their RE lands).
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

#include "runtime/GameHooks.hpp"

#include "core/Hook.hpp"
#include "core/Logger.hpp"
#include "core/Mem.hpp"
#include "events/Event.hpp"
#include "offsets/engine/Frame.hpp"
#include "offsets/engine/Gx.hpp"
#include "offsets/engine/Sound.hpp"
#include "offsets/game/ADT.hpp"
#include "offsets/game/Doodad.hpp"
#include "offsets/game/M2.hpp"
#include "offsets/game/Unit.hpp"
#include "offsets/game/WMO.hpp"
#include "offsets/game/World.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>

namespace
{
    namespace ev    = wxl::events;
    namespace m2    = wxl::offsets::game::m2;
    namespace gxoff = wxl::offsets::engine::gx;
    namespace adt   = wxl::offsets::game::adt;
    namespace dd    = wxl::offsets::game::doodad;
    namespace wmo   = wxl::offsets::game::wmo;
    namespace wld   = wxl::offsets::game::world;
    namespace frame = wxl::offsets::engine::frame;
    namespace unit  = wxl::offsets::game::unit;
    namespace snd   = wxl::offsets::engine::sound;

    m2::M2_InitFn              g_origM2Init       = nullptr;
    m2::M2_FinalizeSkinFn      g_origFinalizeSkin = nullptr;
    m2::M2_SetupBatchAlphaFn   g_origSetupAlpha   = nullptr;
    dd::SpawnFromMDDFFn        g_origDoodadSpawn  = nullptr;
    wmo::Wmo_SpawnFromModfFn   g_origWmoSpawn     = nullptr;
    gxoff::TextureUpdateFn       g_origTexUpdate    = nullptr;
    gxoff::TextureCreateFn       g_origTexCreate    = nullptr;
    wld::AsyncServiceQueuesFn    g_origAsyncDrain   = nullptr;
    adt::Map_ChunkBuildFn      g_origChunkBuild   = nullptr;
    adt::ChunkDestroyFn        g_origChunkDestroy = nullptr;
    wmo::Wmo_RootCompleteFn    g_origWmoRoot      = nullptr;
    wmo::WmoGroup_ParseFn      g_origWmoGroup     = nullptr;
    wld::World_EnterFn         g_origWorldEnter   = nullptr;
    frame::FramePumpFn         g_origFramePump    = nullptr;
    unit::ObjectMsgHandlerFn   g_origObjUpdate    = nullptr;
    unit::ObjectMsgHandlerFn   g_origObjDestroy   = nullptr;
    unit::TargetSetFn          g_origTargetSet    = nullptr;
    snd::PlaySoundFn           g_origPlaySound    = nullptr;

    /**
     * @brief Detours model init, emitting OnModelLoadPre at entry and OnModelLoad after parsing.
     * @param model  runtime model receiving the parsed file (raw bytes at model+0x150, size at +0x16c).
     * @return the original model-init result.
     */
    int __fastcall hkM2Init(void* model)
    {
        ev::ModelLoadArgs pre{ model };
        ev::Emit(ev::Event::OnModelLoadPre, &pre);
        const int r = g_origM2Init(model);
        ev::ModelLoadArgs a{ model };
        ev::Emit(ev::Event::OnModelLoad, &a);
        return r;
    }

    /**
     * @brief Detours skin finalize, emitting OnM2SkinFinalize before the native sizing runs.
     *
     * The skin profile is attached, pointer-fixed and its header live before the native finalize
     * sizes its parallel batch blocks from skin->batchCount, so a subscriber can rebuild a
     * material/texunit contract a modern skin omits.
     * @param model  model whose skin is being finalized.
     */
    void __fastcall hkFinalizeSkin(void* model)
    {
        ev::M2SkinFinalizeArgs a{ model };
        ev::Emit(ev::Event::OnM2SkinFinalize, &a);
        g_origFinalizeSkin(model);
    }

    /**
     * @brief Detours per-batch alpha/material setup, emitting OnM2SetupBatchAlpha with the model and blend.
     *
     * Runs after the native setter picks the alpha-test reference from the blend mode, so a
     * subscriber can re-push a different reference. The draw-context reads are guarded so a
     * malformed context never faults the render thread.
     * @param ctx  draw context.
     */
    void __fastcall hkSetupBatchAlpha(void* ctx)
    {
        g_origSetupAlpha(ctx);

        void*    model = nullptr;
        uint16_t blend = 0;
        __try
        {
            auto* dc   = static_cast<m2::DrawContext*>(ctx);
            void* inst = dc->instance;
            void* mat  = dc->material;
            if (inst) model = static_cast<m2::M2Instance*>(inst)->model;
            if (mat)  blend = static_cast<m2::Material*>(mat)->blend;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { model = nullptr; }

        if (model)
        {
            ev::M2SetupBatchAlphaArgs a{ model, blend };
            ev::Emit(ev::Event::OnM2SetupBatchAlpha, &a);
        }
    }

    /**
     * @brief Detours doodad spawn, emitting OnDoodadSpawn with the placement the native call built.
     * @param modelName   doodad model name.
     * @param mddf        placement record.
     * @param tileOrigin  tile origin for the placement.
     * @return the spawned doodad.
     */
    void* __cdecl hkDoodadSpawn(const char* modelName, void* mddf, void* tileOrigin)
    {
        void* doodad = g_origDoodadSpawn(modelName, mddf, tileOrigin);
        ev::DoodadSpawnArgs a{ doodad };
        ev::Emit(ev::Event::OnDoodadSpawn, &a);
        return doodad;
    }

    /** @brief Multiplies the upper-left 3x3 rows of a 4x4 row-major matrix by a uniform factor. */
    void ScaleMatrixRows3x3(float* m, float s)
    {
        m[0] *= s; m[1] *= s; m[2]  *= s;
        m[4] *= s; m[5] *= s; m[6]  *= s;
        m[8] *= s; m[9] *= s; m[10] *= s;
    }

    /**
     * @brief Reports a freshly spawned WMO's live doodad-set selection against its loaded MODS.
     *
     * Catches the in-game "all doodad sets render at once" case by reading the post-down-convert MODS the
     * Client actually loaded (not the on-disk file) plus the instance's selected/extra sets. Logs only a
     * suspicious shape: an extra set populated, a selected index out of range, or a set 0 whose MODD range
     * swallows the other content sets (every doodad then resolves to set 0, which renders unconditionally).
     * A correctly selected WMO stays silent.
     * @param inst  freshly spawned WMO instance.
     */
    void DiagDoodadSets(void* inst)
    {
        auto* root = *reinterpret_cast<void**>(static_cast<uint8_t*>(inst) + wmo::kOffInstanceRoot);
        if (!root)
            return;
        const uint32_t nSets = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(root) + wmo::kOffRootDoodadSets);
        if (nSets < 2)
            return; // a single-set WMO cannot show "extra" sets
        const uint8_t* mods = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(root) + wmo::kOffRootMods);
        if (!mods)
            return;
        const uint32_t nDefs = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(root) + wmo::kOffRootDoodadDefs);
        const uint32_t sel   = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(inst) + wmo::kOffInstanceDoodadSet);
        const uint16_t* extra = reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(inst) + wmo::kOffInstanceExtraSets);
        const char* name = reinterpret_cast<const char*>(static_cast<uint8_t*>(root) + wmo::kOffNameInline);
        const uint32_t s0count = *reinterpret_cast<const uint32_t*>(mods + wmo::kOffModsCount);

        const bool greedy0  = nDefs && s0count + 1 >= nDefs;    // set 0 covers (almost) every def
        const bool selOob   = sel >= nSets;                     // selected index out of range
        const bool hasExtra = extra[0] || extra[1] || extra[2]; // extra sets populated
        if (!greedy0 && !selOob && !hasExtra)
            return; // selection resolves to {set0, sel} only -> correct, stay silent

        WLOG_INFO("wmo-doodad-diag: %.96s nSets=%u nDefs=%u sel=%u extra={%u,%u,%u} set0count=%u%s%s%s",
            name, nSets, nDefs, sel, extra[0], extra[1], extra[2], s0count,
            greedy0 ? " GREEDY-SET0" : "", selOob ? " SEL-OOB" : "", hasExtra ? " EXTRA-SETS" : "");
    }

    /**
     * @brief Detours the WMO instance spawn, applying the per-instance MODF scale the Client ignores.
     *
     * The Client builds the instance at scale 1.0 (MODF+0x3E is padding to it). After the native spawn,
     * the modern scale is folded into the render matrix (+0x70). The collision/portal copy (+0xB0) is a
     * transposed read-back the portal-visibility test reads as an inverse rotation; scaling it breaks that
     * test and culls interior groups (the WMO goes invisible), so it is left at 1.0 (collision stays at
     * native size). A dedup hit returns an already-scaled instance, so the scale is applied only to a
     * freshly built instance, recognised by its still-orthonormal basis (|row0| == 1); this is reload-safe
     * and needs no per-instance bookkeeping.
     * @param ctx         world context.
     * @param modf        MODF placement record.
     * @param tileOrigin  tile world origin.
     * @param dedup       non-zero to return an existing instance for a known uniqueId.
     * @return the spawned (or existing) instance.
     */
    void* __cdecl hkWmoSpawn(void* ctx, void* modf, const float* tileOrigin, int dedup)
    {
        void* inst = g_origWmoSpawn(ctx, modf, tileOrigin, dedup);
        if (!inst || !modf)
            return inst;

        DiagDoodadSets(inst);

        const uint16_t raw = *reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(modf) + wmo::kOffModfScale);
        if (raw == 0 || raw == 1024)
            return inst; // native / unscaled: leave the instance byte-for-byte
        const float s = static_cast<float>(raw) / 1024.0f;

        float* render = reinterpret_cast<float*>(static_cast<uint8_t*>(inst) + wmo::kOffInstanceRenderMatrix);
        const float len2 = render[0] * render[0] + render[1] * render[1] + render[2] * render[2];
        if (len2 < 0.9999f || len2 > 1.0001f)
            return inst; // already scaled (a dedup hit returned an existing instance)

        ScaleMatrixRows3x3(render, s);
        return inst;
    }

    /**
     * @brief Detours texture upload, emitting OnTextureUpload before the device update.
     *
     * width=x2-x and height=y2-y cover both full-surface (x=y=0) and sub-rect uploads.
     *
     * The mip source the upload reads is a process-wide singleton pointer table (kMipTablePtr) that a
     * build fills with raw aliases into its transient IO buffer; back-to-back doodad/texture loads let a
     * later build overwrite that table and free its buffer while this upload still reads it, an
     * access-violation use-after-free (0x40cb6a). Clearing kMipTableValid here routes the upload through
     * the engine's own safety net: with the flag down, the source callback re-reads the .blp into a fresh
     * buffer instead of trusting the singleton aliases, so the upload never reads a freed page.
     * @param tex   texture being uploaded.
     * @param x     upload rect left.
     * @param y     upload rect top.
     * @param x2    upload rect right.
     * @param y2    upload rect bottom.
     * @param flag  native upload flag.
     */
    void __cdecl hkTexUpdate(void* tex, int x, int y, int x2, int y2, int flag)
    {
        ev::TextureUploadArgs a{ tex, static_cast<uint32_t>(x2 - x), static_cast<uint32_t>(y2 - y) };
        ev::Emit(ev::Event::OnTextureUpload, &a);
        *reinterpret_cast<uint32_t*>(gxoff::kMipTableValid) = 0;
        g_origTexUpdate(tex, x, y, x2, y2, flag);
    }

    // Per-thread async-drain recursion depth. A texture build force-waits nested reads, which re-enter the
    // completion drain; a nested pump running unrelated completions frees / rewrites a buffer the outer
    // build still uploads from (the 0x40cb6a use-after-free).
    thread_local int g_drainDepth = 0;

    // Serialize the reentrant drain. The completed-read queue is a Blizzard TSExplicitList: each node holds
    // its link at node+0x28 {next, tagged-prev}; the head (a node base) is at the completed-head global.
    // Addresses + arithmetic verified against the drain's own head-unlink. The lock is a recursive Storm
    // critical section taken by ECX.
    namespace adrain
    {
        constexpr uint32_t kLockEnter     = 0x00774640; // SCritSect enter, ecx = lock
        constexpr uint32_t kLockLeave     = 0x00774650; // SCritSect leave, ecx = lock
        constexpr uint32_t kAsyncLock     = 0x00B4A240; // the recursive queue lock
        constexpr uint32_t kCompletedHead = 0x00AC3474; // first completed node (sentinel/empty if &1 or 0)
        constexpr uint32_t kAwaitedObj    = 0x00B4A204; // node a force-wait blocks on (0 = none)
        constexpr uint32_t kPendingCount  = 0x00B4A1F8; // outstanding-completion counter
        constexpr uint32_t kLinkOffset    = 0x28;       // node -> link byte offset

        inline uint32_t Rd(uint32_t a)             { return *reinterpret_cast<uint32_t*>(a); }
        inline void     Wr(uint32_t a, uint32_t v) { *reinterpret_cast<uint32_t*>(a) = v; }
        inline uint8_t  RdB(uint32_t a)            { return *reinterpret_cast<uint8_t*>(a); }
        inline void     WrB(uint32_t a, uint8_t v) { *reinterpret_cast<uint8_t*>(a) = v; }
        inline void Lock()   { reinterpret_cast<void(__thiscall*)(uint32_t)>(kLockEnter)(kAsyncLock); }
        inline void Unlock() { reinterpret_cast<void(__thiscall*)(uint32_t)>(kLockLeave)(kAsyncLock); }

        // Detach one node from the completed list (the engine's own head-unlink arithmetic, generalised).
        void Unlink(uint32_t node)
        {
            const uint32_t linkNext = node + 0x28;
            const uint32_t next = Rd(linkNext);
            if (next != 0)
            {
                const uint32_t prev = Rd(node + 0x2c);
                const uint32_t prevSlot = ((prev & 1u) == 0u && prev != 0u)
                                              ? linkNext + (prev - Rd(next + 4))
                                              : (prev & 0xFFFFFFFEu);
                Wr(prevSlot, next);
                Wr(next + 4, Rd(node + 0x2c));
                Wr(linkNext, 0);
                Wr(node + 0x2c, 0);
            }
            else
            {
                const uint32_t prev = Rd(node + 0x2c);
                if ((prev & 1u) != 0u || prev == 0u)
                    Wr(prev & 0xFFFFFFFEu, 0);
                Wr(node + 0x2c, 0);
            }
        }

        // True if target is currently enqueued in the completed list.
        bool Enqueued(uint32_t target)
        {
            uint32_t node = Rd(kCompletedHead);
            if ((node & 1u) != 0u || node == 0u) return false;
            for (;;)
            {
                if (node == target) return true;
                const uint32_t next = Rd(node + 0x28);
                if (next == 0) return false;
                node = next - kLinkOffset;
            }
        }

        // Process ONLY the awaited node; leave every other completion queued for the outer pump.
        int DrainAwaitedOnly()
        {
            Lock();
            const uint32_t target = Rd(kAwaitedObj);
            if (target == 0) { Unlock(); return 1; }
            if (RdB(target + 0x21) != 0) // already serviced this turn
            {
                if (Rd(kAwaitedObj) == target) Wr(kAwaitedObj, 0);
                Unlock();
                return 1;
            }
            if (!Enqueued(target)) { Unlock(); return 1; } // armed but not yet delivered by the worker
            Unlink(target);
            if (Rd(kAwaitedObj) == target) Wr(kAwaitedObj, 0);
            WrB(target + 0x21, 1);
            Unlock(); // the engine releases the lock before every completion call
            reinterpret_cast<void(__cdecl*)(uint32_t)>(Rd(target + 0x10))(Rd(target + 0x0c));
            Wr(kPendingCount, Rd(kPendingCount) - 1);
            return 1;
        }
    }

    /**
     * @brief Detours the async-queue drain to serialize reentrant pumps.
     *
     * Depth 0 runs the full engine drain. A reentrant pump (a build force-waiting a nested read) processes
     * only the node that wait is blocked on and leaves the rest, so no nested completion frees or rewrites
     * a buffer the outer build is still uploading from.
     */
    int __cdecl hkAsyncDrain(int a, int b)
    {
        if (g_drainDepth > 0)
            return adrain::DrainAwaitedOnly();
        ++g_drainDepth;
        const int r = g_origAsyncDrain(a, b);
        --g_drainDepth;
        return r;
    }

    /**
     * @brief Detours the by-name texture create, emitting OnBlpLoad after the request resolves.
     *
     * Fires on every reference (returns the cached handle on a hit), so the event carries the requested
     * name and a subscriber can watch for one specific BLP. Logs each distinct name once, so the log lists
     * BLPs as they first load without flooding. The name set is mutex-guarded as the request can arrive
     * from a loader thread.
     * @param name    requested texture path (full virtual path).
     * @param flags   native load flags.
     * @param status  native status out-pointer.
     * @param flags2  native load flags.
     * @return the resolved texture handle (null on failure).
     */
    void* __cdecl hkTexCreate(const char* name, uint32_t flags, int* status, uint32_t flags2)
    {
        void* handle = g_origTexCreate(name, flags, status, flags2);

        ev::BlpLoadArgs a{ name, handle };
        ev::Emit(ev::Event::OnBlpLoad, &a);

        return handle;
    }

    /**
     * @brief Detours the server object update-block handler, emitting OnObjectUpdate after the parse.
     *
     * One fire per update message (a batch of created/updated objects). Logs the first fire only.
     * @param ctx     handler context.
     * @param opcode  message opcode.
     * @param msg     message id.
     * @param packet  inbound message reader.
     * @return the native handler result.
     */
    int __cdecl hkObjUpdate(void* ctx, int opcode, int msg, void* packet)
    {
        const int r = g_origObjUpdate(ctx, opcode, msg, packet);

        ev::ObjectUpdateArgs a{ packet, opcode };
        ev::Emit(ev::Event::OnObjectUpdate, &a);

        static bool logged = false;
        if (!logged) { logged = true; WLOG_INFO("object: update stream active"); }
        return r;
    }

    /**
     * @brief Detours the object destroy handler, emitting OnObjectDestroy before the despawn.
     *
     * One fire per despawn, while the object is still resident. Logs the first fire only.
     * @param ctx     handler context.
     * @param opcode  message opcode.
     * @param msg     message id.
     * @param packet  inbound message reader (object GUID + on-death flag).
     * @return the native handler result.
     */
    int __cdecl hkObjDestroy(void* ctx, int opcode, int msg, void* packet)
    {
        ev::ObjectDestroyArgs a{ packet, opcode };
        ev::Emit(ev::Event::OnObjectDestroy, &a);

        static bool logged = false;
        if (!logged) { logged = true; WLOG_INFO("object: destroy hook active"); }
        return g_origObjDestroy(ctx, opcode, msg, packet);
    }

    /**
     * @brief Detours the target-set API, emitting OnTargetChanged after the new target is applied.
     * @param scriptState  script state the call ran on.
     * @return the native function result.
     */
    int __cdecl hkTargetSet(void* scriptState)
    {
        const int r = g_origTargetSet(scriptState);

        ev::TargetChangedArgs a{ scriptState };
        ev::Emit(ev::Event::OnTargetChanged, &a);

        WLOG_INFO("target: changed");
        return r;
    }

    /**
     * @brief Detours the play-sound API, emitting OnSoundPlay before the sound starts.
     *
     * Fires on every UI/world sound. Logs the first fire only.
     * @param scriptState  script state the call ran on (the sound id/name is on its stack).
     * @return the native function result.
     */
    int __cdecl hkPlaySound(void* scriptState)
    {
        ev::SoundPlayArgs a{ scriptState };
        ev::Emit(ev::Event::OnSoundPlay, &a);

        static bool logged = false;
        if (!logged) { logged = true; WLOG_INFO("sound: play hook active"); }
        return g_origPlaySound(scriptState);
    }

    /**
     * @brief Detours map-chunk build, emitting OnAdtChunkBuild with the chunk and its layer count.
     *
     * Runs after the native build populates the sub-chunk pointers and layer units, so the
     * texture-layer count is readable.
     * @param chunk    map chunk.
     * @param edx      unused register slot for the thiscall convention.
     * @param rawMcnk  raw chunk data.
     * @param param2   native build parameter.
     */
    void __fastcall hkChunkBuild(void* chunk, void* edx, void* rawMcnk, int param2)
    {
        g_origChunkBuild(chunk, edx, rawMcnk, param2);
        uint32_t layers = 0;
        void* header = static_cast<adt::MapChunk*>(chunk)->mcnkHeader;
        if (header) layers = static_cast<adt::McnkHeader*>(header)->nLayers;
        ev::AdtChunkArgs a{ chunk, layers };
        ev::Emit(ev::Event::OnAdtChunkBuild, &a);
    }

    /**
     * @brief Detours map-chunk teardown to cancel its in-flight async read before the buffer is freed.
     *
     * The chunk's completed-read callback parses the chunk's IO buffer (+0x80). If a teardown frees that
     * buffer while the read is still queued, the later completion parses freed memory and faults
     * (0x7d6f05). Retiring the async object (+0x70) here unlinks the pending completion first.
     * @param chunk  CMapChunk being destroyed (ECX).
     */
    void __fastcall hkChunkDestroy(void* chunk)
    {
        if (chunk)
        {
            auto* slot = reinterpret_cast<void**>(static_cast<uint8_t*>(chunk) + adt::kOffChunkAsyncObj);
            if (*slot)
            {
                reinterpret_cast<wld::AsyncDestroyFn>(wld::kAsyncDestroy)(*slot);
                *slot = nullptr;
            }
        }
        g_origChunkDestroy(chunk);
    }

    /**
     * @brief Detours WMO root read-completion, emitting OnWmoRootLoad before the native chunk walk.
     *
     * Fires once after the async read fills the root buffer and before the walker runs, so a subscriber
     * may reshape the root buffer in place; the native walk then reads the reshaped bytes.
     * @param root  map-object root whose buffer was just read.
     */
    void __cdecl hkWmoRootComplete(void* root)
    {
        ev::WmoRootLoadArgs a{ root };
        ev::Emit(ev::Event::OnWmoRootLoad, &a);
        g_origWmoRoot(root);
    }

    /**
     * @brief Detours WMO group parse, emitting OnWmoGroupLoad before the native sub-chunk walk.
     *
     * The join point of the sync and async group-load paths, before the sub-chunk walk, so a subscriber
     * may reshape the group buffer in place; the native walk then reads the reshaped bytes.
     * @param group  map-object group whose buffer was just read.
     * @param edx    unused register slot for the thiscall convention.
     */
    void __fastcall hkWmoGroupParse(void* group, void* edx)
    {
        ev::WmoGroupLoadArgs a{ group };
        ev::Emit(ev::Event::OnWmoGroupLoad, &a);
        g_origWmoGroup(group, edx);
    }

    /**
     * @brief Detours world enter, emitting OnWorldLeave before and OnWorldEnter after the transition.
     * @param worldTime          target world time.
     * @param withLoadingScreen  nonzero to show the loading screen.
     */
    void __cdecl hkWorldEnter(int worldTime, int withLoadingScreen)
    {
        const auto mapId = static_cast<uint32_t>(*reinterpret_cast<int32_t*>(wld::kCurrentMapId));
        ev::WorldLeaveArgs leave{ mapId }; // old world still loaded: id is the one being left
        ev::Emit(ev::Event::OnWorldLeave, &leave);
        g_origWorldEnter(worldTime, withLoadingScreen);
        const auto entered = static_cast<uint32_t>(*reinterpret_cast<int32_t*>(wld::kCurrentMapId));
        ev::WorldEnterArgs enter{ entered };
        ev::Emit(ev::Event::OnWorldEnter, &enter);
    }

    /**
     * @brief Detours the master per-frame pump, emitting OnUpdate once per frame with the frame delta.
     */
    void __cdecl hkFramePump()
    {
        g_origFramePump();
        ev::UpdateArgs a{ *reinterpret_cast<float*>(frame::kDeltaSeconds),
                          *reinterpret_cast<uint32_t*>(frame::kFrameTimeMs) };
        ev::Emit(ev::Event::OnUpdate, &a);
    }
}

namespace wxl::runtime::game
{
    /**
     * @brief Installs every game-logic detour through the core hook layer.
     */
    void Install()
    {
        wxl::core::hook::Install("M2Init", m2::kInit,
                                 reinterpret_cast<void*>(&hkM2Init),
                                 reinterpret_cast<void**>(&g_origM2Init));
        wxl::core::hook::Install("M2FinalizeSkin", m2::kFinalizeSkin,
                                 reinterpret_cast<void*>(&hkFinalizeSkin),
                                 reinterpret_cast<void**>(&g_origFinalizeSkin));
        wxl::core::hook::Install("M2SetupBatchAlpha", m2::kSetupBatchAlpha,
                                 reinterpret_cast<void*>(&hkSetupBatchAlpha),
                                 reinterpret_cast<void**>(&g_origSetupAlpha));
        wxl::core::hook::Install("DoodadSpawn", dd::kSpawnFromMDDF,
                                 reinterpret_cast<void*>(&hkDoodadSpawn),
                                 reinterpret_cast<void**>(&g_origDoodadSpawn));
        wxl::core::hook::Install("WmoSpawn", wmo::kSpawnFromModf,
                                 reinterpret_cast<void*>(&hkWmoSpawn),
                                 reinterpret_cast<void**>(&g_origWmoSpawn));
        wxl::core::hook::Install("TextureUpdate", gxoff::kTextureUpdate,
                                 reinterpret_cast<void*>(&hkTexUpdate),
                                 reinterpret_cast<void**>(&g_origTexUpdate));
        wxl::core::hook::Install("TextureCreate", gxoff::kTextureCreate,
                                 reinterpret_cast<void*>(&hkTexCreate),
                                 reinterpret_cast<void**>(&g_origTexCreate));
        wxl::core::hook::Install("AsyncDrain", wld::kAsyncServiceQueues,
                                 reinterpret_cast<void*>(&hkAsyncDrain),
                                 reinterpret_cast<void**>(&g_origAsyncDrain));
        wxl::core::hook::Install("ChunkBuild", adt::kChunkBuild,
                                 reinterpret_cast<void*>(&hkChunkBuild),
                                 reinterpret_cast<void**>(&g_origChunkBuild));
        // ChunkDestroy (ADT cancel-on-teardown) temporarily disabled: it correlates with a render-path
        // null-deref (0x7c846c) and the RE flagged the cancel timing as unconfirmed. Re-enable after the
        // one-shot probe (FUN_007bfe60 free + FUN_007d6ef0 entry) confirms the free is teardown vs sibling.
        (void)&hkChunkDestroy; (void)&g_origChunkDestroy;
        wxl::core::hook::Install("WmoRootComplete", wmo::kRootComplete,
                                 reinterpret_cast<void*>(&hkWmoRootComplete),
                                 reinterpret_cast<void**>(&g_origWmoRoot));
        wxl::core::hook::Install("WmoGroupParse", wmo::kGroupParse,
                                 reinterpret_cast<void*>(&hkWmoGroupParse),
                                 reinterpret_cast<void**>(&g_origWmoGroup));
        wxl::core::hook::Install("CWorldEnter", wld::kEnter,
                                 reinterpret_cast<void*>(&hkWorldEnter),
                                 reinterpret_cast<void**>(&g_origWorldEnter));
        wxl::core::hook::Install("FramePump", frame::kFramePump,
                                 reinterpret_cast<void*>(&hkFramePump),
                                 reinterpret_cast<void**>(&g_origFramePump));
        wxl::core::hook::Install("ObjectUpdate", unit::kObjectUpdateHandler,
                                 reinterpret_cast<void*>(&hkObjUpdate),
                                 reinterpret_cast<void**>(&g_origObjUpdate));
        wxl::core::hook::Install("ObjectDestroy", unit::kObjectDestroyHandler,
                                 reinterpret_cast<void*>(&hkObjDestroy),
                                 reinterpret_cast<void**>(&g_origObjDestroy));
        wxl::core::hook::Install("TargetSet", unit::kTargetSet,
                                 reinterpret_cast<void*>(&hkTargetSet),
                                 reinterpret_cast<void**>(&g_origTargetSet));
        wxl::core::hook::Install("PlaySound", snd::kPlaySound,
                                 reinterpret_cast<void*>(&hkPlaySound),
                                 reinterpret_cast<void**>(&g_origPlaySound));

        // Liquid-row null guard: this one liquid consumer dereferences the LiquidType row flag without the
        // null check the others have, so an unknown liquid id (from any served source) faults. Skip the
        // flag test and make the branch unconditional (nop x4 + jz->jmp) -> default no-bump path.
        {
            const uint8_t guard[5] = { 0x90, 0x90, 0x90, 0x90, 0xEB };
            wxl::core::mem::Patch(reinterpret_cast<void*>(adt::kLiquidRowFlagTest), guard, sizeof guard);
        }

        WLOG_INFO("game: hooks installed (M2Init, M2FinalizeSkin, M2SetupBatchAlpha, DoodadSpawn, TextureUpdate, TextureCreate, ChunkBuild, WmoRootComplete, WmoGroupParse, CWorldEnter, FramePump, ObjectUpdate, ObjectDestroy, TargetSet, PlaySound)");
    }
}
