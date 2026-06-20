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
        OnFrame,         // per-frame Present                          (FrameArgs)
        OnEndScene,      // end of the frame, before present           (EndSceneArgs)
        OnWorldRender,   // per-frame world draw pass                  (WorldRenderArgs)
        OnWorldRenderEnd,// world -> UI boundary, the post-fx slot     (WorldRenderEndArgs)
        OnM2BatchDraw,   // one M2 triangle batch is drawing           (M2BatchDrawArgs)
        OnM2SetupBatchAlpha, // an M2 batch's alpha/material is set up (M2SetupBatchAlphaArgs)
        OnRibbonDraw,    // a ribbon emitter is about to draw          (RibbonDrawArgs)
        OnInput,         // window input message (swallowable)         (InputArgs)
        OnAdtChunkBuild, // an ADT map chunk is being built            (AdtChunkArgs)
        OnTextureUpload, // a texture is about to upload to the device (TextureUploadArgs)
        OnDoodadSpawn,   // a placed map doodad (CMapDoodad) was built (DoodadSpawnArgs)
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
    /** @brief Args for OnEndScene. */
    struct EndSceneArgs       { void* device; };
    /** @brief Args for OnWorldRender. */
    struct WorldRenderArgs    { void* device; };
    /** @brief Args for OnWorldRenderEnd. */
    struct WorldRenderEndArgs { void* device; };
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
    /** @brief Args for OnAdtChunkBuild. */
    struct AdtChunkArgs      { void* chunk; uint32_t layerCount; };
    /** @brief Args for OnTextureUpload. */
    struct TextureUploadArgs { void* texture; uint32_t width; uint32_t height; };
    /** @brief Args for OnDoodadSpawn; read the transform via wxl::game::doodad. */
    struct DoodadSpawnArgs   { void* doodad; };
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
}
