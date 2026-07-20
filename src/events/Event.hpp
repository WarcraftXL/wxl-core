// Event bus: the readable hook surface modules subscribe to. The core owns the detours.
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

/// Event bus: the core installs the detours and republishes them as named events. A module
/// Subscribe()s and never sees an address. A subscriber is a plain function pointer plus an opaque
/// user, so Emit() walks a flat vector with no std::function or vtable on the path. Each event
/// carries a typed POD args struct by const pointer.
namespace wxl::events
{
    /// Named events a module may subscribe to; each carries the args struct noted alongside it.
    enum class Event : uint32_t
    {
        OnModelLoadPre,  // a model's raw bytes are read, before parse (ModelLoadArgs)
        OnModelLoad,     // a model finished loading and is parsed     (ModelLoadArgs)
        OnM2SkinFinalize,// a model's skin profile is being finalized  (M2SkinFinalizeArgs)
        OnM2PerFrameUpdate, // per-render-ctx scene-graph update tick  (M2PerFrameUpdateArgs)
        OnBuildBonePalette, // bone-palette fill for one instance, post-engine (BuildBonePaletteArgs)
        OnFrame,         // per-frame Present                          (FrameArgs)
        OnEndScene,      // end of the frame, before present           (EndSceneArgs)
        OnDeviceLost,    // before IDirect3DDevice9::Reset frees DEFAULT resources (DeviceResetArgs)
        OnDeviceReset,   // after a successful IDirect3DDevice9::Reset  (DeviceResetArgs)
        OnUpdate,        // once-per-frame logic tick, with delta time (UpdateArgs)
        OnWorldRender,   // per-frame world draw pass                  (WorldRenderArgs)
        OnWorldRenderEnd,// world -> UI boundary, the post-fx slot     (WorldRenderEndArgs)
        OnLiquidRender,  // a liquid render pass is about to draw       (LiquidRenderArgs)
        OnM2BatchDraw,   // one M2 triangle batch is drawing           (M2BatchDrawArgs)
        OnM2SetupBatchAlpha, // an M2 batch's alpha/material is set up (M2SetupBatchAlphaArgs)
        OnRibbonDraw,    // a ribbon emitter is about to draw          (RibbonDrawArgs)
        OnInput,         // window input message (swallowable)         (InputArgs)
        OnWorldClick,    // a world click resolved to a point/object   (WorldClickArgs)
        OnAdtChunkBuild, // an ADT map chunk is being built            (AdtChunkArgs)
        OnWmoRootLoad,   // a WMO root buffer is read, before the walk (WmoRootLoadArgs)
        OnWmoGroupLoad,  // a WMO group buffer is read, before the walk(WmoGroupLoadArgs)
        OnTextureUpload, // a texture is about to upload to the device (TextureUploadArgs)
        OnBlpLoad,       // a BLP texture was requested by name           (BlpLoadArgs)
        OnObjectUpdate,  // a server object create/update batch parsed     (ObjectUpdateArgs)
        OnObjectDestroy, // an object is about to despawn                  (ObjectDestroyArgs)
        OnTargetChanged, // the player's target was set via the API        (TargetChangedArgs)
        OnSoundPlay,     // a UI/world sound is about to play              (SoundPlayArgs)
        OnDoodadSpawn,   // a placed map doodad (CMapDoodad) was built (DoodadSpawnArgs)
        OnItemSlotChange,// a character model slot received an item    (ItemSlotChangeArgs)
        OnItemSlotClear, // a character model equipment slot was cleared(ItemSlotClearArgs)
        OnWeaponVisualChange, // a unit's mainhand/offhand/ranged visible-item entry field was written
                              // (WeaponVisualChangeArgs)
        OnWorldEnter,    // the world/map finished loading, in-world   (WorldEnterArgs)
        OnWorldLeave,    // the world/map is being torn down           (WorldLeaveArgs)
        OnBeforeHostLaunch, // the DLL is about to launch the asset host (HostLaunchArgs)
        Count
    };

    // Typed args, passed by const void* and reinterpreted by the subscriber for its event.

    /** @brief Args for OnModelLoadPre and OnModelLoad. */
    struct ModelLoadArgs      { void* model; };
    /**
     * @brief Args for OnM2SkinFinalize, fired before the native finalize sizes its parallel batch
     *        blocks; the window to rebuild a material/texunit contract a modern skin omits (read the
     *        header and skin via wxl::game::m2::Header / Skin). The header arrays are raw pointers here.
     */
    struct M2SkinFinalizeArgs { void* model; };
    /** @brief Args for OnFrame. */
    struct FrameArgs          { void* device; };
    /** @brief Args for OnUpdate, fired once per frame with the frame delta and timestamp. */
    struct UpdateArgs         { float dt; uint32_t timeMs; };
    /** @brief Args for OnEndScene. */
    struct EndSceneArgs       { void* device; };
    /**
     * @brief Args for OnDeviceLost (fired before the engine resets the D3D9 device -- e.g. a window resize --
     *        while every D3DPOOL_DEFAULT resource must be released) and OnDeviceReset (fired after a successful
     *        reset, so a subscriber recreates them). params is the D3DPRESENT_PARAMETERS* the reset (re)creates with.
     */
    struct DeviceResetArgs    { void* device; void* params; };
    /** @brief Args for OnWorldRender. */
    struct WorldRenderArgs    { void* device; };
    /**
     * @brief Args for OnWorldRenderEnd. When supersampling is active, superSampleSource is the factor-sized
     *        offscreen world surface (IDirect3DSurface9*) the world rendered into and ssaaFactor is its scale
     *        relative to the native backbuffer; a subscriber downsamples that surface into the native
     *        backbuffer. superSampleSource is null (and ssaaFactor 1.0) when supersampling is off. depthSource
     *        is the readable (INTZ) world depth surface (IDirect3DSurface9*) for depth-using effects, or null
     *        when no readable depth was bound this frame. proj is an optional row-major float[16] projection
     *        override (same layout as the in-world camera projection global) for depth-using effects: the world
     *        boundary leaves it null (the subscriber reads the live world camera), while the glue boundary
     *        passes the glue scene's projection, whose camera is not in the world camera globals.
     */
    struct WorldRenderEndArgs { void* device; void* superSampleSource; float ssaaFactor; void* depthSource; const float* proj; };
    /**
     * @brief Args for OnLiquidRender, fired before the native liquid pass draws. passType is 0 for the
     *        main pass, 1 for the secondary; instanceCount is the visible liquid instances in this pass;
     *        transform is the shared liquid transform forwarded to each instance. Read-only.
     */
    struct LiquidRenderArgs  { void* bank; void* transform; int passType; uint32_t instanceCount; };
    /**
     * @brief Args for OnM2BatchDraw, fired just after the native draw with the same draw parameters,
     *        so a subscriber can re-issue the draw while the vertex/index buffers are still bound.
     */
    struct M2BatchDrawArgs
    {
        void*    device;
        void*    model;
        int      primType;
        int      baseVertex;
        uint32_t minIndex;
        uint32_t numVerts;
        uint32_t startIndex;
        uint32_t primCount;
    };
    /**
     * @brief Args for OnM2SetupBatchAlpha, fired just after the native setter chose the alpha-test
     *        reference from the blend mode; a subscriber may re-push the reference
     *        (wxl::game::m2::PushAlphaRef) for the model and blend mode it recognizes. blendMode 1 is
     *        alpha key. model may be null, in which case the subscriber skips.
     */
    struct M2SetupBatchAlphaArgs { void* model; uint16_t blendMode; };
    /**
     * @brief Args for OnRibbonDraw, fired before the native draw; a subscriber sets *useMultiTexture
     *        true to request the single-pass multi-texture combine for a ribbon of >= 3 layers (the
     *        core binds the extra layers and folds the passes into one). useMultiTexture is never null
     *        and starts false.
     */
    struct RibbonDrawArgs { void* emitter; uint32_t layerCount; bool* useMultiTexture; };
    /**
     * @brief Args for OnInput; a subscriber that consumes the message sets *handled true, which makes
     *        the core swallow it so the game does not also react. handled is never null. Args are
     *        otherwise read-only.
     */
    struct InputArgs         { uint32_t message; uintptr_t wparam; uintptr_t lparam; bool* handled; };
    /**
     * @brief Args for OnWorldClick, fired on a mouse button when the cursor ray hits the world (not when
     *        the click was consumed by a UI subscriber). hitType is 2 for an M2/doodad, 3 for terrain or
     *        WMO; x/y/z is the world hit point; objLo/objHi is the engine object handle (zero for terrain).
     */
    struct WorldClickArgs    { uint32_t message; int hitType; float x; float y; float z; void* objLo; void* objHi; };
    /** @brief Args for OnAdtChunkBuild. */
    struct AdtChunkArgs      { void* chunk; uint32_t layerCount; };
    /**
     * @brief Args for OnWmoRootLoad, fired after the WMO root buffer is read and before the native chunk
     *        walk; the window to reshape the root in place (read/replace the buffer via wxl::game::wmo).
     */
    struct WmoRootLoadArgs   { void* root; };
    /**
     * @brief Args for OnWmoGroupLoad, fired after a WMO group buffer is read and before the native
     *        sub-chunk walk; the window to reshape the group in place (read/replace via wxl::game::wmo).
     */
    struct WmoGroupLoadArgs  { void* group; };
    /** @brief Args for OnTextureUpload. */
    struct TextureUploadArgs { void* texture; uint32_t width; uint32_t height; };
    /**
     * @brief Args for OnBlpLoad, fired after a by-name texture request resolves. name is the full virtual
     *        path requested (match case-insensitively, slash-normalized); handle is the resolved texture
     *        handle (may be null on failure). Fires on every reference, cached or not. Read-only.
     */
    struct BlpLoadArgs       { const char* name; void* handle; };
    /**
     * @brief Args for OnObjectUpdate, fired after a server update message is parsed (a batch of objects
     *        created or field-updated). packet is the inbound message reader (cursor already consumed);
     *        opcode is the message opcode. Read-only.
     */
    struct ObjectUpdateArgs  { void* packet; int opcode; };
    /**
     * @brief Args for OnObjectDestroy, fired before an object despawns while it is still resident. packet
     *        holds the object GUID and an on-death flag; opcode is the message opcode. Read-only.
     */
    struct ObjectDestroyArgs { void* packet; int opcode; };
    /**
     * @brief Args for OnTargetChanged, fired after the target-set API applied a new target. scriptState is
     *        the script state the call ran on; read the applied target GUID via wxl::game (kTargetGuid).
     */
    struct TargetChangedArgs { void* scriptState; };
    /**
     * @brief Args for OnSoundPlay, fired before a UI/world sound plays. scriptState is the script state
     *        the call ran on; the sound id/name is on its stack. Read-only.
     */
    struct SoundPlayArgs     { void* scriptState; };
    /** @brief Args for OnDoodadSpawn; read the transform via wxl::game::doodad. */
    struct DoodadSpawnArgs   { void* doodad; };
    /** @brief Args for OnItemSlotChange; charModelObj is the CharModelObject, modelSlot is the internal
     *         model slot index (maps to an equipment category), itemDataPtr points to the item data block. */
    struct ItemSlotChangeArgs { void* charModelObj; uint32_t modelSlot; void* itemDataPtr; };
    /** @brief Args for OnItemSlotClear; charModelObj is the CharModelObject, equipSlotWow is the
     *         WoW equipment slot index (EQUIPMENT_SLOT_* constants, 0-18). */
    struct ItemSlotClearArgs  { void* charModelObj; uint32_t equipSlotWow; };
    /**
     * @brief Args for OnWeaponVisualChange. Fires for EVERY object in range whose visible-item entry
     *        field is written, including the initial full-sync when a unit enters view (not only live
     *        equip changes) -- this is when the client itself learns the value, not a semantic "just
     *        equipped" event.
     *        unit is the owning object pointer, or null if kUnitFieldArrayOffset (offsets/game/Unit.hpp)
     *        has not been set yet.
     *        slot is 0 = mainhand, 1 = offhand, 2 = ranged.
     *        itemEntry is the new item entry ID now in that slot (0 = slot cleared).
     */
    struct WeaponVisualChangeArgs { void* unit; uint32_t slot; uint32_t itemEntry; };
    /** @brief Args for OnM2PerFrameUpdate; renderCtx is the per-instance render context that the
     *         scene graph is updating — fires once per visible M2 instance per frame. */
    struct M2PerFrameUpdateArgs { void* renderCtx; };
    /** @brief Args for OnBuildBonePalette; fires after the engine fills the per-instance bone palette
     *         from the current animation pose, immediately before the batch draw uploads it to the
     *         vertex shader. renderCtx is the M2Instance whose bone palette was just written.
     *         Subscribers may overwrite bone matrices here to override the engine's pose. */
    struct BuildBonePaletteArgs { void* renderCtx; };
    /** @brief Args for OnWorldEnter. */
    struct WorldEnterArgs    { uint32_t mapId; };
    /** @brief Args for OnWorldLeave. */
    struct WorldLeaveArgs    { uint32_t mapId; };
    /**
     * @brief Args for OnBeforeHostLaunch; a subscriber may set *cancel true to suppress the
     *        auto-launch (e.g. it manages the host itself). exePath is the path about to be launched.
     *        cancel is never null and starts false. Fired once, before the CreateProcess.
     */
    struct HostLaunchArgs    { const char* exePath; bool* cancel; };

    using Handler = void (*)(void* user, const void* args);

    /**
     * @brief Subscribes a handler to an event, called cold at startup.
     * @param e        event to subscribe to.
     * @param handler  function pointer invoked on Emit.
     * @param user     opaque pointer passed back to the handler.
     */
    void Subscribe(Event e, Handler handler, void* user);

    /**
     * @brief Publishes an event to its subscribers in subscription order, called by the core's detours.
     * @param e     event to publish.
     * @param args  typed args struct for the event, passed by const pointer.
     */
    void Emit(Event e, const void* args);

    /**
     * @brief Reports whether an event has at least one subscriber.
     *
     * For a per-draw detour, checking this before building the args struct skips the whole emission
     * when nothing listens. Subscription happens at module load, before the render hooks run, so the
     * unsynchronized read is stable by the time a hot path consults it.
     * @param e  event to test.
     * @return true when Emit(e, ...) would invoke at least one handler.
     */
    bool Any(Event e);
}
