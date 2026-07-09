// M2 game bindings: typed wrappers over the client's model functions, curated by hand.
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

#include <cstddef>
#include <cstdint>

#include "game/Binding.hpp"
#include "offsets/game/M2.hpp"
#include "structure/m2/M2Format.hpp"

/**
 * @brief Curated M2 bindings: inline typed wrappers over the client's model functions.
 *
 * A module writes wxl::game::m2::ResolveTexture(h) instead of casting an address. The wrappers
 * are zero-overhead inline typed calls; RegisterCatalog() adds the names to the enumerable
 * catalog for tooling and the scripting bridge.
 */
namespace wxl::game::m2
{
    namespace off = wxl::offsets::game::m2;

    /**
     * @brief Resolves a texture handle to the internal texture object a sampler bind expects.
     * @param handle  the texture handle to resolve.
     * @return the resolved internal texture object.
     */
    inline void* ResolveTexture(void* handle)
    {
        return Native<off::M2_TexResolveFn>(off::kTexResolve)(handle, 0, 0);
    }

    /**
     * @brief Binds a resolved texture to a device sampler selector.
     * @param device           the render device.
     * @param selector         the sampler selector to bind to.
     * @param resolvedTexture  the resolved texture object to bind.
     */
    inline void BindSampler(void* device, uint32_t selector, void* resolvedTexture)
    {
        Native<off::M2_SamplerBindFn>(off::kSamplerBind)(device, nullptr, selector, resolvedTexture);
    }

    /**
     * @brief Pushes the alpha-test reference to the device.
     * @param ref  the alpha-test reference value.
     */
    inline void PushAlphaRef(float ref)
    {
        Native<off::M2_PushAlphaRefFn>(off::kPushAlphaRef)(ref);
    }

    // --- raw .m2 file buffer (valid at parse time) ---
    // The whole .m2 file is read into a heap buffer at model+0x150. The byte size is at model+0x16c.

    /**
     * @brief Returns the raw .m2 file buffer attached to a model.
     * @param model  the model object.
     * @return pointer to the in-memory .m2 file buffer.
     */
    inline void*    FileBuffer(void* model) { return static_cast<off::M2Model*>(model)->header; }

    /**
     * @brief Returns the byte size of a model's raw .m2 file buffer.
     * @param model  the model object.
     * @return the buffer size in bytes.
     */
    inline uint32_t FileSize  (void* model) { return static_cast<off::M2Model*>(model)->fileSize; }

    /**
     * @brief Returns the model path stem stored by the native loader.
     * @param model  the model object.
     * @return Inline path stem without extension.
     */
    inline const char* PathStem(void* model) { return static_cast<off::M2Model*>(model)->pathStem; }

    /**
     * @brief Returns the parsed model header.
     * @param model  the model object.
     * @return the header; the .m2 buffer is parsed in place so the buffer base is the header.
     *
     * Valid once the model is parsed. Header M2Arrays hold raw pointers by this point, not file
     * offsets.
     */
    inline wxl::structure::m2::M2Header* Header(void* model)
    {
        return reinterpret_cast<wxl::structure::m2::M2Header*>(FileBuffer(model));
    }

    /**
     * @brief The engine's live parsed skin profile, hung off the model object.
     *
     * Not the on-disk skin: the parse prepends a 4-byte leading field, so the arrays sit 4 bytes
     * higher than the file layout, and the count/pointer pairs are raw pointers. Valid at or after
     * skin finalize. Indices are global into the model vertex/index pools.
     */
#pragma pack(push, 1)
    struct M2SkinProfile
    {
        uint32_t                          _lead;        // 0x00  parse-prepended leading field
        uint32_t                          vertexCount;  // 0x04
        uint16_t*                         vertexLookup; // 0x08
        uint32_t                          indexCount;   // 0x0C
        uint16_t*                         indices;      // 0x10
        uint32_t                          boneCount;    // 0x14
        uint8_t*                          bones;        // 0x18
        uint32_t                          submeshCount; // 0x1C
        wxl::structure::m2::M2SkinSection* submeshes;   // 0x20
        uint32_t                          batchCount;   // 0x24  (texunits)
        wxl::structure::m2::M2Batch*      batches;      // 0x28
        uint32_t                          boneCountMax; // 0x2C  per-draw bone budget seed
    };
#pragma pack(pop)
    static_assert(sizeof(M2SkinProfile) == 0x30, "M2SkinProfile");
    static_assert(offsetof(M2SkinProfile, submeshes) == 0x20, "M2SkinProfile.submeshes");
    static_assert(offsetof(M2SkinProfile, batches) == 0x28, "M2SkinProfile.batches");

    /**
     * @brief Returns the live parsed skin profile for a model.
     * @param model  the model object.
     * @return the skin profile at model+0x170, or null before it is attached.
     */
    inline M2SkinProfile* Skin(void* model)
    {
        return *reinterpret_cast<M2SkinProfile**>(reinterpret_cast<char*>(model) + off::kOffModelSkin);
    }

    /**
     * @brief Allocates a buffer with the same allocator the .m2 load buffer uses.
     * @param size  the buffer size in bytes.
     * @param tag   caller-supplied debug tag, keeping the core version-agnostic.
     * @return the allocated buffer.
     *
     * A replacement buffer allocated here is freed correctly by the model destructor, which relies
     * on the back-shift byte at [ptr-1].
     */
    inline void* AllocBuffer(uint32_t size, const char* tag)
    {
        return Native<off::M2_BufferAllocFn>(off::kBufferAlloc)(size, tag, 0);
    }

    /**
     * @brief Frees a buffer allocated by AllocBuffer.
     * @param ptr  the buffer to free.
     */
    inline void FreeBuffer(void* ptr)
    {
        Native<off::M2_BufferFreeFn>(off::kBufferFree)(ptr);
    }

    /**
     * @brief Points the model at a different .m2 image (buffer plus byte size).
     * @param model   the model object.
     * @param buffer  the new .m2 image buffer.
     * @param size    the new buffer size in bytes.
     *
     * The parser reads these on the next call.
     */
    inline void ReplaceBuffer(void* model, void* buffer, uint32_t size)
    {
        auto* m = static_cast<off::M2Model*>(model);
        m->header   = buffer;
        m->fileSize = size;
    }

    /**
     * @brief Rebuilds the GPU index buffer for a model from its current rawTri (skin->indices).
     * @param model  the model object (ECX for the thiscall).
     *
     * Called directly when the skin profile is already present and the GPU IB needs to be
     * rebuilt with an updated geoset filter. Accepts small per-call leaks of internal buffers
     * (old model+0x18c, model+0x188, [model+0x150]+0x40) as the cost of forcing a re-bake.
     * Always resets rawTri to identity after building the IB; apply filter immediately before.
     */
    inline void FinalizeSkin(void* model)
    {
        Native<off::M2_FinalizeSkinFn>(off::kFinalizeSkin)(model);
    }

    // --- attachment / render-context bindings ---

    /**
     * @brief Returns (or allocates) the per-model render context for a CharModelObject.
     * @param cmo     the CharModelObject this pointer.
     * @param keyBuf  a pointer to a key buffer that identifies the render context slot.
     * @return the render context, or null on failure.
     */
    inline void* GetRenderCtx(void* cmo, void* keyBuf)
    {
        return Native<off::M2_GetRenderCtxFn>(off::kGetRenderCtx)(cmo, nullptr, keyBuf, 0);
    }

    /**
     * @brief Attaches a collection-M2 render context to a scene slot on the parent render context.
     * @param renderCtx   the parent render context (from GetRenderCtx).
     * @param subObj      the collection-M2 instance to attach.
     * @param slot        the scene slot index to attach to.
     * @param forceAttach when true, passes zero2=1 to sub_831630, bypassing the BoneIndicesByID
     *                    LUT check that silently exits for attachment points not present in the
     *                    character's LUT (e.g. point 19 used by collection M2s).
     */
    inline void AttachToScene(void* renderCtx, void* subObj, uint32_t slot, bool forceAttach = false)
    {
        Native<off::M2_AttachToSceneFn>(off::kAttachToScene)(renderCtx, nullptr, subObj, slot, 0, forceAttach ? 1u : 0u);
    }

    /**
     * @brief Detaches the M2 bound to a scene slot, releasing its render context.
     * @param subObj  the collection-M2 instance that owns the slot.
     * @param slot    the scene slot index to detach.
     */
    inline void DetachSlot(void* subObj, uint32_t slot)
    {
        Native<off::M2_DetachSlotFn>(off::kDetachSlot)(subObj, nullptr, slot);
    }

    /**
     * @brief Releases a render context obtained from GetRenderCtx.
     * @param renderCtx  the render context to release.
     */
    inline void ReleaseRenderCtx(void* renderCtx)
    {
        Native<off::M2_ReleaseRenderCtxFn>(off::kReleaseRenderCtx)(renderCtx, nullptr);
    }

    /**
     * @brief Binds the M2 model resource to texture slot key 2 (main texture) on a render context.
     * @param renderCtx  the render context to bind into.
     * @param modelPtr   the M2 resource/model pointer to bind.
     */
    inline void BindTexSlot(void* renderCtx, void* modelPtr)
    {
        Native<off::M2_BindTexSlotFn>(off::kBindTexSlot)(renderCtx, nullptr, 2, modelPtr);
    }

    /**
     * @brief Loads a resource by virtual path through the global resource-loader object.
     * @param path   the virtual path (e.g. "Item\\ObjectComponents\\Weapon\\AxeSmall.M2").
     * @param flags  loader flags (0 = default synchronous load).
     * @return the resource handle, or null on failure.
     */
    inline void* LoadResource(const char* path, uint32_t flags = 0)
    {
        int status = 0;
        return Native<off::M2_LoadResourceFn>(off::kLoadResource)(path, flags, &status, 0);
    }

    /**
     * @brief Releases a resource handle returned by LoadResource.
     * @param resource  the resource handle to release.
     */
    inline void ReleaseResource(void* resource)
    {
        Native<off::M2_ReleaseResourceFn>(off::kReleaseResource)(resource);
    }

    /** @brief Adds the M2 bindings to the enumerable catalog. */
    inline void RegisterCatalog()
    {
        Register({ "M2::ResolveTexture",  off::kTexResolve,       "void*(handle)" });
        Register({ "M2::BindSampler",     off::kSamplerBind,      "void(device, selector, tex)" });
        Register({ "M2::PushAlphaRef",    off::kPushAlphaRef,     "void(float ref)" });
        Register({ "M2::GetRenderCtx",    off::kGetRenderCtx,     "void*(cmo, keyBuf)" });
        Register({ "M2::AttachToScene",   off::kAttachToScene,    "void(renderCtx, subObj, slot)" });
        Register({ "M2::DetachSlot",      off::kDetachSlot,       "void(subObj, slot)" });
        Register({ "M2::ReleaseRenderCtx",off::kReleaseRenderCtx, "void(renderCtx)" });
        Register({ "M2::BindTexSlot",     off::kBindTexSlot,      "void(renderCtx, modelPtr)" });
        Register({ "M2::LoadResource",    off::kLoadResource,     "void*(path, flags)" });
        Register({ "M2::ReleaseResource", off::kReleaseResource,  "void(resource)" });
    }
}
