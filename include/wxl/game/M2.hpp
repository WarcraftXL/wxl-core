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

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/game/M2.hpp"
#include "wxl/formats/M2Format.hpp"

/**
 * @brief Curated M2 bindings: inline typed wrappers over the client's model functions.
 *
 * A module writes wxl::game::m2::ResolveTexture(h) instead of casting an address. The wrappers
 * are zero-overhead inline typed calls.
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
     * @brief Thin typed facade over one M2 model object: the raw .m2 file buffer, its parsed
     *        header, live skin profile, and the two mutating operations that replace/rebuild them.
     *
     * Each method is a zero-overhead inline read/call; method names mirror the Get/Set convention
     * `wxl::game::gx::Device9` already established for this SDK layer.
     */
    class M2Model
    {
    public:
        /**
         * @brief Wraps a raw M2 model pointer.
         * @param model  the raw model pointer, possibly null.
         */
        explicit M2Model(void* model) : model_(model) {}

        /** @brief Returns the wrapped raw model pointer. */
        void* raw() const { return model_; }

        /** @brief Returns true if a model is wrapped. */
        explicit operator bool() const { return model_ != nullptr; }

        // --- raw .m2 file buffer (valid at parse time) ---
        // The whole .m2 file is read into a heap buffer at model+0x150. The byte size is at model+0x16c.

        /** @brief Returns the raw .m2 file buffer attached to this model. */
        void* GetFileBuffer() const { return static_cast<off::M2Model*>(model_)->header; }

        /** @brief Returns the byte size of this model's raw .m2 file buffer. */
        uint32_t GetFileSize() const { return static_cast<off::M2Model*>(model_)->fileSize; }

        /** @brief Returns the model path stem stored by the native loader. */
        const char* GetPathStem() const { return static_cast<off::M2Model*>(model_)->pathStem; }

        /**
         * @brief Returns the parsed model header.
         * @return the header; the .m2 buffer is parsed in place so the buffer base is the header.
         *
         * Valid once the model is parsed. Header M2Arrays hold raw pointers by this point, not
         * file offsets.
         */
        wxl::structure::m2::M2Header* GetHeader() const
        {
            return reinterpret_cast<wxl::structure::m2::M2Header*>(GetFileBuffer());
        }

        /** @brief This model's bounding sphere radius (standard MD20 field). */
        float GetBoundingSphereRadius() const { return GetHeader()->boundingSphereRadius; }

        /** @brief This model's particle emitter count. */
        uint32_t GetParticleEmitterCount() const { return GetHeader()->particleEmitters.count; }

        /**
         * @brief Returns the live parsed skin profile for this model.
         * @return the skin profile at model+0x170, or null before it is attached.
         */
        M2SkinProfile* GetSkin() const
        {
            return At<M2SkinProfile*>(model_, off::kOffModelSkin);
        }

        /**
         * @brief Points this model at a different .m2 image (buffer plus byte size).
         * @param buffer  the new .m2 image buffer.
         * @param size    the new buffer size in bytes.
         *
         * The parser reads these on the next call.
         */
        void SetBuffer(void* buffer, uint32_t size)
        {
            auto* m = static_cast<off::M2Model*>(model_);
            m->header   = buffer;
            m->fileSize = size;
        }

        /**
         * @brief Rebuilds the GPU index buffer for this model from its current rawTri (skin->indices).
         *
         * Called directly when the skin profile is already present and the GPU IB needs to be
         * rebuilt with an updated geoset filter. Accepts small per-call leaks of internal buffers
         * (old model+0x18c, model+0x188, [model+0x150]+0x40) as the cost of forcing a re-bake.
         * Always resets rawTri to identity after building the IB; apply filter immediately before.
         */
        void FinalizeSkin()
        {
            Native<off::M2_FinalizeSkinFn>(off::kFinalizeSkin)(model_);
        }

    private:
        void* model_;
    };

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

    // --- attachment / render-context bindings ---

    /**
     * @brief Builds a NEW scene model for a model path. Every call creates one -- nothing is looked
     *        up or reused -- so the caller owns the result and must release what it stops using.
     * @param cmo     the scene the model is created in.
     * @param keyBuf  the model path to load.
     * @return the new scene model, or null on failure.
     */
    inline void* GetRenderCtx(void* cmo, void* keyBuf)
    {
        return Native<off::M2_GetRenderCtxFn>(off::kCreateSceneModel)(cmo, nullptr, keyBuf, 0);
    }

    /**
     * @brief Attaches a collection-M2 render context to a scene slot on the parent render context.
     * @param renderCtx   the parent render context (from GetRenderCtx).
     * @param subObj      the collection-M2 instance to attach.
     * @param slot        the scene slot index to attach to.
     * @param forceAttach when true, bypasses the attachment-point lookup check that otherwise silently
     *                    exits for attachment points not present in the character's table (e.g. point 19
     *                    used by collection M2s).
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
}
