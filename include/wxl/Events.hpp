// The events an extension subscribes to: the ids, generated from events/Events.def, and the args
// struct each one carries. Every args field is a POD; handlers receive the struct by const pointer.
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

#include "wxl/Common.h"

#include <cstdint>

namespace wxl::events
{
    /**
     * Args of OnModelLoadPre and OnModelLoad.
     *
     * @param void* model : the model object
     */
    struct ModelLoadArgs      { void* model; };

    /**
     * Args of OnM2SkinFinalize. The header arrays are raw pointers at this point; read the header
     * and the skin through wxl::game::m2.
     *
     * @param void* model : the model object
     */
    struct M2SkinFinalizeArgs { void* model; };

    /**
     * Args of OnM2NativeLoad.
     *
     * @param void* model : the model object
     * @param uint32 version : the model's modern inner version (272..274)
     * @param uint32 texturesResolved : TXID FileDataID resolutions that succeeded in this load
     * @param uint32 texturesUnresolved : TXID FileDataID resolutions that failed in this load
     * @param uint32 skipMask : the modern payloads the reader parked, one bit each
     */
    struct M2NativeLoadArgs
    {
        void*    model;
        uint32_t version;
        uint32_t texturesResolved;
        uint32_t texturesUnresolved;
        uint32_t skipMask;
    };

    /**
     * Args of OnFrame.
     *
     * @param void* device : the IDirect3DDevice9
     */
    struct FrameArgs          { void* device; };

    /**
     * Args of OnUpdate.
     *
     * @param float dt : seconds since the previous frame
     * @param uint32 timeMs : the frame timestamp in milliseconds
     */
    struct UpdateArgs         { float dt; uint32_t timeMs; };

    /**
     * Args of OnEndScene.
     *
     * @param void* device : the IDirect3DDevice9
     */
    struct EndSceneArgs       { void* device; };

    /**
     * Args of OnDeviceLost and OnDeviceReset. A handler releases its D3DPOOL_DEFAULT resources on the
     * first and recreates them on the second.
     *
     * @param void* device : the IDirect3DDevice9
     * @param void* params : the D3DPRESENT_PARAMETERS the reset creates with
     */
    struct DeviceResetArgs    { void* device; void* params; };

    /**
     * Args of OnWorldRender.
     *
     * @param void* device : the IDirect3DDevice9
     */
    struct WorldRenderArgs    { void* device; };

    /**
     * Args of OnWorldRenderEnd: the world is drawn, the interface is not yet.
     *
     * @param void* device : the IDirect3DDevice9
     */
    struct WorldRenderEndArgs { void* device; };

    /**
     * Args of OnWorldSceneEnd: the world is drawn and the camera matrices that drew it are still on
     * the device, so geometry placed by world coordinate lands where those coordinates say. The
     * caller restores the pre-world projection and view right after.
     *
     * @param void* device : the IDirect3DDevice9
     * @param void* sceneDepth : the depth-stencil surface the world was drawn into, may be null
     */
    struct WorldSceneEndArgs  { void* device; void* sceneDepth; };

    /**
     * Args of OnLiquidRender, read-only.
     *
     * @param void* bank : the liquid bank
     * @param void* transform : the shared liquid transform
     * @param int32 passType : 0 for the main pass, 1 for the secondary
     * @param uint32 instanceCount : visible liquid instances in this pass
     */
    struct LiquidRenderArgs  { void* bank; void* transform; int passType; uint32_t instanceCount; };

    /**
     * Args of OnM2BatchDraw: the parameters of the draw that just ran, with its buffers still bound.
     *
     * @param void* device : the IDirect3DDevice9
     * @param void* model : the model object
     * @param int32 primType
     * @param int32 baseVertex
     * @param uint32 minIndex
     * @param uint32 numVerts
     * @param uint32 startIndex
     * @param uint32 primCount
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
     * Args of OnM2SetupBatchAlpha. A handler may re-push the alpha reference with
     * wxl::game::m2::PushAlphaRef.
     *
     * @param void* model : the model object, may be null
     * @param uint16 blendMode : the batch blend mode; 1 is alpha key
     */
    struct M2SetupBatchAlphaArgs { void* model; uint16_t blendMode; };

    /**
     * Args of OnRibbonDraw.
     *
     * @param void* emitter : the ribbon emitter
     * @param uint32 layerCount : the emitter's texture layers
     * @param bool* useMultiTexture : set true to request the single-pass multi-texture combine; starts false
     */
    struct RibbonDrawArgs { void* emitter; uint32_t layerCount; bool* useMultiTexture; };

    /**
     * Args of OnInput.
     *
     * @param uint32 message : the window message
     * @param uintptr wparam
     * @param uintptr lparam
     * @param bool* handled : set true to swallow the message; the game never sees it
     */
    struct InputArgs         { uint32_t message; uintptr_t wparam; uintptr_t lparam; bool* handled; };

    /**
     * Args of OnWorldClick. Does not fire when a UI handler consumed the click.
     *
     * @param uint32 message : the mouse message
     * @param int32 hitType : 2 for an M2 or doodad, 3 for terrain or WMO
     * @param float x
     * @param float y
     * @param float z
     * @param void* objLo : low half of the engine object handle, zero for terrain
     * @param void* objHi : high half of the engine object handle, zero for terrain
     */
    struct WorldClickArgs    { uint32_t message; int hitType; float x; float y; float z; void* objLo; void* objHi; };

    /**
     * Args of OnAdtChunkBuild.
     *
     * @param void* chunk : the map chunk
     * @param uint32 layerCount : its texture layers
     */
    struct AdtChunkArgs      { void* chunk; uint32_t layerCount; };

    /**
     * Args of OnAdtSplitTileLoad, read-only. Fires on the main thread after the stock tile parser ran.
     *
     * @param int32 tileFirst : the first %d of "<Map>_%d_%d.adt"
     * @param int32 tileSecond : the second %d
     * @param uint32 rootSize : resident size of the root file, 0 when absent
     * @param uint32 texSize : resident size of the _tex0 file, 0 when absent
     * @param uint32 objSize : resident size of the _obj0 file, 0 when absent
     * @param uint32 chunkCount : MCNKs indexed from the root; 256 on a well-formed tile
     */
    struct AdtSplitTileLoadArgs
    {
        int      tileFirst;
        int      tileSecond;
        uint32_t rootSize;
        uint32_t texSize;
        uint32_t objSize;
        uint32_t chunkCount;
    };

    /**
     * Args of OnWmoRootLoad: the window to reshape the root in place through wxl::game::wmo.
     *
     * @param void* root : the root buffer
     */
    struct WmoRootLoadArgs   { void* root; };

    /**
     * Args of OnWmoGroupLoad: the window to reshape the group in place through wxl::game::wmo.
     *
     * @param void* group : the group buffer
     */
    struct WmoGroupLoadArgs  { void* group; };

    /**
     * Args of OnTextureUpload.
     *
     * @param void* texture : the texture object
     * @param uint32 width
     * @param uint32 height
     */
    struct TextureUploadArgs { void* texture; uint32_t width; uint32_t height; };

    /**
     * Args of OnBlpLoad, read-only.
     *
     * @param string name : the full virtual path requested; match case-insensitively, slash-normalized
     * @param void* handle : the resolved texture handle, null on failure
     */
    struct BlpLoadArgs       { const char* name; void* handle; };

    /**
     * Args of OnObjectUpdate, read-only.
     *
     * @param void* packet : the inbound message reader, cursor already consumed
     * @param int32 opcode : the message opcode
     */
    struct ObjectUpdateArgs  { void* packet; int opcode; };

    /**
     * Args of OnObjectDestroy, read-only.
     *
     * @param void* packet : holds the object GUID and the on-death flag
     * @param int32 opcode : the message opcode
     */
    struct ObjectDestroyArgs { void* packet; int opcode; };

    /**
     * Args of OnTargetChanged. The applied target GUID is read through wxl::game.
     *
     * @param void* scriptState : the script state the call ran on
     */
    struct TargetChangedArgs { void* scriptState; };

    /**
     * Args of OnSoundPlay, read-only. The sound id or name is on the script stack.
     *
     * @param void* scriptState : the script state the call ran on
     */
    struct SoundPlayArgs     { void* scriptState; };

    /**
     * Args of OnDoodadSpawn. The transform is read through wxl::game::doodad.
     *
     * @param void* doodad : the doodad object
     */
    struct DoodadSpawnArgs   { void* doodad; };

    /**
     * Args of OnItemSlotChange.
     *
     * @param void* charModelObj : the CharModelObject
     * @param uint32 modelSlot : the internal model slot index, one per equipment category
     * @param void* itemDataPtr : the item data block
     */
    struct ItemSlotChangeArgs { void* charModelObj; uint32_t modelSlot; void* itemDataPtr; };

    /**
     * Args of OnItemSlotClear.
     *
     * @param void* charModelObj : the CharModelObject
     * @param uint32 equipSlotWow : the equipment slot, EQUIPMENT_SLOT_* 0..18
     */
    struct ItemSlotClearArgs  { void* charModelObj; uint32_t equipSlotWow; };

    /**
     * Args of OnM2PerFrameUpdate.
     *
     * @param void* renderCtx : the per-instance render context being updated
     */
    struct M2PerFrameUpdateArgs { void* renderCtx; };

    /**
     * Args of OnBuildBonePalette. A handler may overwrite bone matrices to override the pose.
     *
     * @param void* renderCtx : the M2Instance whose palette was written
     */
    struct BuildBonePaletteArgs { void* renderCtx; };

    /**
     * Args of OnWorldEnter.
     *
     * @param uint32 mapId : the map entered
     */
    struct WorldEnterArgs    { uint32_t mapId; };

    /**
     * Args of OnWorldLeave.
     *
     * @param uint32 mapId : the map being left
     */
    struct WorldLeaveArgs    { uint32_t mapId; };

    /**
     * Args of OnGrassWind, read-only. Runtime tuning goes through the wxl.wind service.
     *
     * @param float dirX : damped wind vector, x
     * @param float dirY : damped wind vector, y
     * @param float strength : its magnitude
     * @param float phase : accumulated sway phase in radians
     */
    struct GrassWindArgs     { float dirX; float dirY; float strength; float phase; };

    /**
     * Args of OnAdtHeightBlend, read-only. Fires on the draw thread.
     *
     * @param uint32 layerCount : the permutation's texture layers (2..4)
     * @param uint32 stockBytes : bytecode size before the injection
     * @param uint32 patchedBytes : bytecode size after it
     */
    struct AdtHeightBlendArgs { uint32_t layerCount; uint32_t stockBytes; uint32_t patchedBytes; };

    /// The events, by their table id. Count is one past the highest id.
    enum class Event : uint32_t
    {
#define WXL_EVENT(name, id, args) name = id,
#include "wxl/events/Events.def"
#undef WXL_EVENT
        Count = 0
#define WXL_EVENT(name, id, args) + 1
#include "wxl/events/Events.def"
#undef WXL_EVENT
    };

    /// The args struct of an event: Args<Event::OnUpdate> is UpdateArgs.
    template <Event E> struct ArgsOf;
#define WXL_EVENT(name, id, args) template <> struct ArgsOf<Event::name> { using type = args; };
#include "wxl/events/Events.def"
#undef WXL_EVENT
    template <Event E> using Args = typename ArgsOf<E>::type;

    /**
     * The event's name, for logs.
     *
     * @param Event e
     * @return string name : "?" past Count
     */
    inline constexpr const char* Name(Event e)
    {
        switch (e)
        {
#define WXL_EVENT(name, id, args) case Event::name: return #name;
#include "wxl/events/Events.def"
#undef WXL_EVENT
        default: return "?";
        }
    }

    namespace detail
    {
        // The ids are 0..Count-1 with no gap and no repeat: a hole or a duplicate in the table fails here.
        constexpr bool IdsAreDense()
        {
            bool seen[static_cast<uint32_t>(Event::Count)] = {};
#define WXL_EVENT(name, id, args) if ((id) >= static_cast<uint32_t>(Event::Count) || seen[id]) return false; seen[id] = true;
#include "wxl/events/Events.def"
#undef WXL_EVENT
            return true;
        }
        static_assert(IdsAreDense(), "Events.def: ids must be 0..Count-1, each used once");
    }

    /// A handler as the bus calls it: the opaque user pointer and the event's args struct.
    using Handler = void(WXL_CDECL*)(void* user, const void* args);
}
