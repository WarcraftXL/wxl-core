// Programmable-shader load/select/bind seam: device shader create/bind, the two-step effect bind,
// the selection-state globals, and the WMO exterior effect table.
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

// INTERNAL to the core. The client's programmable D3D9 shader path: how a compiled block becomes a live
// D3D9 shader on the device, how a permutation is activated then bound, and the render-state globals the
// selection reads. The own modern-shader stack mirrors the create/bind calls and intercepts the bind so
// it can substitute its own shaders for a modern effect while native effects stay on the native path.
// Modules never include this; the modern-shaders runtime script does.
namespace wxl::offsets::engine::shader
{
    // --- D3D9 device shader create / bind (on *(gx::kGxDevicePtr + gx::kD3DDeviceField)) ------------
    // The create/bind calls go straight through the stock IDirect3DDevice9 vtable; indices are shared
    // with gx::vt (CreateVertexShader 91, CreatePixelShader 106, SetVertexShader 92, SetPixelShader 107).
    // CreateVertexShader(byteCode, &out) / CreatePixelShader(byteCode, &out) take the raw bytecode and
    // return an IDirect3D*Shader9*. SetVertexShader/SetPixelShader bind that handle.

    // --- The two-step effect bind (the takeover seam) ---------------------------------------------
    // Both gated by the programmable-path master flag (kProgrammablePathFlag). Step 1 activates a
    // collection (writes kActiveCollection), step 2 binds one permutation out of it by index.
    constexpr uintptr_t kEffectActivate = 0x00872F90; // (collection): kActiveCollection = collection
    constexpr uintptr_t kEffectBind     = 0x00873060; // (vtxIdx, pixIdx): bind slot[vtxIdx]/slot[pixIdx]

    // Outdoor/sky-aware index driver feeding kEffectBind:
    //   vtxIdx = (kLightBit != 0) + (min(kShadowTier, 2) * 15 + kSubIndex) * 2
    //   pixIdx = kShadowTier + kShadowGroup * 4
    constexpr uintptr_t kOutdoorIndexDriver = 0x007A84D0;
    constexpr uintptr_t kPixelIndexDriver   = 0x00872DE0;

    // The native BLS load path (kept as a landmark; the own stack does NOT call it). It hard-checks the
    // container version and fills a collection's fixed 90-vertex / 16-pixel positional slot arrays.
    // It also reaches a container by requesting "<base>\<profile>\<name>.bls" through the ordinary
    // content path -- which is the seam a container of our own is delivered through, with no detour.
    constexpr uintptr_t kNativeBlsLoad = 0x00684970;

    // --- terrain shader sets (landmarks for the owned-container work) ------------------------------
    // One set per shadow configuration, each holding the whole permutation family a terrain draw picks
    // from. A container we repack is loaded into these exactly like the stock one, so they are
    // diagnostics landmarks -- nothing in the height blend writes them.
    constexpr uintptr_t kTerrainShaderSetNoShadow   = 0x00CE0408;
    constexpr uintptr_t kTerrainShaderSetShadowLow  = 0x00CE0388;
    constexpr uintptr_t kTerrainShaderSetShadowHigh = 0x00CE0208;
    constexpr uintptr_t kTerrainShaderSetSolid      = 0x00CE0488;
    constexpr uintptr_t kTerrainShaderSetSolidEnv   = 0x00CE0004;
    // Turns (layer count, coverage family, layer-mask, reflected-layer, shadow tier) into the
    // positional index of a permutation within one of the sets above. Landmark: a repack that keeps
    // block order needs no index arithmetic of its own.
    constexpr uintptr_t kTerrainShaderPermutationIndex = 0x0079E5C0;
    // Per-frame terrain shader selection: refreshes the active-shader table the bucket loop binds
    // from. Landmark for the alternative delivery in which the table entries themselves are replaced.
    constexpr uintptr_t kTerrainShaderSelect = 0x007D3E10;

    // --- per-shader D3D create seam (below the effect-collection stack) ---------------------------
    // The device's vertex-shader create entry: the point where a shader wrapper's bytecode becomes a
    // live IDirect3DVertexShader9. Reads the wrapper's bytecode pointer (+kCgxShaderBytePtr) and length
    // (+kCgxShaderByteLen), calls the device CreateVertexShader (D3D vtbl +0x16c), and stores the handle
    // at +kCgxShaderHandle. __thiscall(device /*ecx*/, wrapper /*one stack arg*/), ret 4. Detour
    // as __fastcall(device, edx, wrapper) -- byte-compatible -- to substitute a recognised shader's
    // bytecode (swap the wrapper's +0x50/+0x4c fields around the original call, then restore).
    constexpr uintptr_t kShaderCreateVertex = 0x006AA0D0;
    using ShaderCreateVertexFn = void(__fastcall*)(void* device, void* edx, void* wrapper);

    // Global cdecl helper that uploads programmable-shader constants through the live device
    // (device vtbl +0x118, __thiscall): (target 0=vertex/4=pixel, startReg, const float* data, numVec4).
    // No-ops when data is null.
    constexpr uintptr_t kShaderConstantsSet = 0x00408210;
    using ShaderConstantsSetHelperFn = void(__cdecl*)(int target, int startReg, const float* data, int numVec4);

    // The profile tables the .bls path is built from: six entries, one per EGxShTarget, each a
    // const char*[] indexed by that target's caps level. IShaderLoad formats
    // "<dir>\<profileName[target][level]>\<name>.bls", so these strings ARE the folder names on
    // disk -- vertex level 3 is "vs_3_0", pixel level 4 is "ps_3_0", pixel level 3 is "ps_2_0".
    // Read out of the image rather than inferred.
    constexpr uintptr_t kProfileTables = 0x00AD8890; // const char* const* [6], by EGxShTarget

    // --- the shadow constant caches (CGxDevice::s_shadowConstants) -------------------------------
    // Where ShaderConstantsSet puts a constant. One cache per target, each exactly 256 vec4
    // registers, followed by the 8-byte dirty range the flush uploads from. The device constructor
    // fills both with 0x7F7FFFFF and sets the range to [0, 255].
    //
    // The setter computes `base + reg * 16` and bounds-checks NOTHING -- its only tests are
    // `target == 0` and `count == 0`. So a pixel write at register 256 lands on the pixel dirty
    // pair and register 257 onward lands INSIDE the vertex cache. Anything substituting its own
    // shaders owns that bound itself; ps_3_0's 224 float constants fit, but nothing enforces it.
    constexpr uintptr_t kConstCachePixel  = 0x00C5DFE0; // 256 x C4Vector
    constexpr uintptr_t kConstCacheVertex = 0x00C5EFE8; // 256 x C4Vector
    constexpr uint32_t  kConstRegisters   = 256;        // per target, registers 0..255
    constexpr size_t    kConstCacheBytes  = kConstRegisters * 16;
    // The dirty range of each cache, {low, high} inclusive register indices, at the end of its block.
    constexpr uintptr_t kConstDirtyPixel  = kConstCachePixel + kConstCacheBytes;  // 0x00C5EFE0
    constexpr uintptr_t kConstDirtyVertex = kConstCacheVertex + kConstCacheBytes; // 0x00C5FFE8
    /// Resolves a target's cache and dirty range for the flush. __thiscall, 23 bytes.
    constexpr uintptr_t kShaderConstantsLock = 0x00683560;

    // --- the shader-effect constant block (which engine register carries what) --------------------
    // Register conventions are the engine's, not the backend's, so substituting a shader for a world
    // effect means honouring them. This is the block the fog and alpha-test values are uploaded
    // from: two adjacent vec4s, pushed through ShaderConstantsSet (slot 70) by the CShaderEffect
    // setters below.
    //
    //   +0x00  PIXEL  c2  = (fogColor.r, fogColor.g, fogColor.b, alphaRef)
    //   +0x10  VERTEX c30 = (fogScale, fogBias, fogExponent, 0)
    //
    // Two things about it are easy to get wrong, and both are read out of the image:
    //
    //  - the colour order. The source is a CImVector, which is BGRA in memory, and the setter writes
    //    param[2], param[1], param[0] -- so the register receives R, G, B in xyz, each divided by
    //    255. Taking the CImVector's own order would silently swap red and blue.
    //  - the sharing. SetFogParams and SetAlphaRef write different components of the SAME pixel
    //    register and each re-uploads the whole vec4, which is why an alpha reference lives in what
    //    reads like a colour. A consumer of either value must expect the other to be refreshed
    //    under it.
    //
    // The vertex half is the same fog one register along: the exponent is what the paired vertex
    // shader raises its depth factor to (`mad r0.x, r0.z, c30.x, c30.y` then `pow r1.x, r0.x,
    // c30.z`), and the result reaches the pixel shader as the FOG interpolator, saturated, 1 meaning
    // unfogged.
    constexpr uintptr_t kEffectConstants             = 0x00D43058;
    constexpr size_t    kEffectConstFogColorAlphaRef = 0x00; // -> pixel c2
    constexpr size_t    kEffectConstFogParams        = 0x10; // -> vertex c30
    constexpr uint32_t  kEffectPixelRegFogColor      = 2;
    constexpr uint32_t  kEffectVertexRegFogParams    = 30;

    // Registers the paired vertex shaders read, confirmed in
    // Shaders\Vertex\vs_3_0\MapObjDiffuse_T1.bls permutation 0 rather than inferred from a setter.
    constexpr uint32_t kEffectVertexRegProjection = 2;  // c2..c5,   transpose(ProjNative)
    constexpr uint32_t kEffectVertexRegWorldView  = 31; // c31..c33, three rows

    /// CShaderEffect::SetFogParams(start, end, exponent, const CImVector* color). __cdecl. Writes
    /// both halves of kEffectConstants and uploads the pixel one.
    constexpr uintptr_t kEffectSetFogParams = 0x00873210;
    /// CShaderEffect::SetAlphaRef(ref). __thiscall. `ref` is 0..1, NOT 0..255 -- the call sites
    /// divide by 255 first. Writes kEffectConstants +0x0C and re-uploads pixel c2 whole.
    constexpr uintptr_t kEffectSetAlphaRef = 0x00873BA0;
    /// CShaderEffect::SetFogEnabled(on). __cdecl. Uploads vertex c30 on the programmable path.
    constexpr uintptr_t kEffectSetFogEnabled = 0x00873390;
    /// CShaderEffect::UpdateProjMatrix: transpose(ProjNative) into vertex c2..c5.
    constexpr uintptr_t kEffectUpdateProjMatrix = 0x00872C10;
    /// CShaderEffectManager::GetEffect(name) -> the effect object, looked up by the effect's name.
    /// The effect-to-shader pairing is data rather than code: shaders\effects\*.wfx in the archives
    /// is plain text, and it is what says which vertex shader sits opposite which pixel shader, and
    /// what the fixed-function path does instead.
    constexpr uintptr_t kEffectManagerGetEffect = 0x00876530;

    // --- selection-state globals (the live inputs the own stack reads instead of a positional slot) --
    constexpr uintptr_t kShadowTier          = 0x00D43010; // shadow tier, clamped 0..2
    constexpr uintptr_t kShadowGroup         = 0x00D43014; // pixel shadow group
    constexpr uintptr_t kProgrammablePathFlag = 0x00D43020; // CShaderEffect::s_enableShaders: programmable path active
    constexpr uintptr_t kActiveCollection    = 0x00D43024; // active effect collection (set by activate)
    constexpr uintptr_t kLightBit            = 0x00CFBEAC; // light/fog bit (0 or 1)
    constexpr uintptr_t kSubIndex            = 0x00CFBEB4; // permutation sub-index 0..14

    // --- collection object layout ------------------------------------------------------------------
    // The per-effect object the native loader fills: one slot per permutation. The own stack does not
    // read these slots for a modern effect; kept for the native-path landmark and the activate match.
    constexpr size_t kCollectionVtxSlots = 0x2C;  // vertex permutation slot array (90 entries)
    constexpr size_t kCollectionPixSlots = 0x194; // pixel permutation slot array (16 entries)

    // --- GxState shader cache (why a direct device SetShader does NOT stick) -----------------------
    // kEffectBind does NOT call the device directly. It writes the chosen shader-wrapper pointers into a
    // GxState cache (state 0x4d vertex, 0x4e pixel) on the graphics-device object (gx::kGxDevicePtr =
    // this). The device SetVertexShader/SetPixelShader happen LATER, at the deferred GxState flush before
    // the draw, which reads each slot's wrapper and applies its live handle at wrapper+kCgxShaderHandle.
    // So a direct device set from a detour is overwritten by that flush (it re-applies the cached native
    // slot, which is null for a rejected-version modern effect -> null pixel shader -> black). The own
    // stack must therefore write its OWN wrapper into the same cache via the state setter below.
    constexpr uintptr_t kGxStateSet      = 0x00685F50; // CGxDevice::RsSet, __thiscall(this=gx device, stateIdx, value)
    // Marks one GxState slot dirty so the next flush re-applies its CACHED value: the coherent way to
    // hand a stage back to the engine after a raw device bind bypassed the cache for one draw.
    constexpr uintptr_t kGxStateDirty    = 0x00685970; // __thiscall(this=gx device, stateIdx)
    using GxStateDirtyFn = void(__thiscall*)(void* gxDevice, unsigned stateIdx);
    constexpr unsigned  kStateVertexShader = 0x4D;     // GxState slot the flush applies as vertex shader
    constexpr unsigned  kStatePixelShader  = 0x4E;     // GxState slot the flush applies as pixel shader
    // Texture-stage slots: 16 consecutive states, one texture object per sampler. The setter itself
    // range-checks `state - 0x15 < 0x10` for its async-touch side path, which pins the layout.
    constexpr unsigned  kStateTexture0     = 0x15;     // + stage index (0..15)
    using GxStateSetFn = void(__thiscall*)(void* gxDevice, unsigned stateIdx, void* value);

    // --- shader wrapper layout (the native per-permutation shader object) -----------------------
    // The GxState flush applies a slot by reading the WRAPPER at the slot: if its created flag (+0x30) is
    // set it binds the live handle (+0x20) straight to the device, else it re-creates from the bytecode
    // pointer (+0x50)/length (+0x4c). The own stack therefore builds a minimal wrapper: live handle at
    // +0x20 and created flag = 1 at +0x30 (so the flush binds our handle and never tries to re-create).
    // Live handle at +0x20, created flag +0x30, bytecode length +0x4c, bytecode pointer +0x50.
    // Identified at runtime by its RTTI type descriptor.
    constexpr size_t kCgxShaderHandle    = 0x20;
    constexpr size_t kCgxShaderCreated   = 0x30; // non-zero = handle ready, skip re-create at flush
    constexpr size_t kCgxShaderByteLen   = 0x4C;
    constexpr size_t kCgxShaderBytePtr   = 0x50;
    constexpr size_t kCgxShaderWrapBytes = 0x60; // allocation size that covers all fields the flush reads
    constexpr uintptr_t kCgxShaderRtti  = 0x00AD8CA8;

    // --- WMO exterior effect table -----------------------------------------------------------------
    // Filled at WMO subsystem init by name: each slot is a registered effect-collection pointer. Order:
    // Diffuse, Specular, Metal, Env, Opaque, EnvMetal. The Opaque collection pointer (index 4) is the
    // Phase-1 discriminator: when kActiveCollection equals *kExteriorEffectOpaque the active draw is the
    // exterior-opaque effect. The slot holds the same pointer kActiveCollection is set to on activate.
    constexpr uintptr_t kExteriorEffectTable  = 0x00D1C3F0; // [0]=Diffuse .. [5]=EnvMetal
    constexpr uintptr_t kExteriorEffectOpaque = 0x00D1C400; // [4] MapObjOpaque collection pointer
    // [6] MapObjComposite (the two-layer effect). When kActiveCollection equals *kExteriorEffectComposite
    // the active draw is the two-layer composite: the seam where a modern WMO's second layer must be
    // composited by the texture's own alpha instead of the secondary vertex-colour alpha the stock PS uses.
    constexpr uintptr_t kExteriorEffectComposite = 0x00D1C408; // [6]

    // The client keeps TWO adjacent 7-entry effect-collection tables. Ext/IntRender bind from 0xD1C3F0
    // above; but a WMO whose root has MOHD.flags & 0x2 (every modern WMO) is DELEGATED to AltRender
    // (0x007A9380), which binds from a SEPARATE table based at 0x00D1C3D4. So a modern Composite batch
    // sets kActiveCollection to *kAltEffectComposite, never *kExteriorEffectComposite -- the Composite
    // discriminator must accept the Alt-table slot to fire on modern content.
    constexpr uintptr_t kAltEffectTable     = 0x00D1C3D4; // [0]=Diffuse .. used by AltRender (modern path)
    constexpr uintptr_t kAltEffectComposite = 0x00D1C3EC; // [6] MapObjComposite, modern delegated path

    // Plain two-arg cdecl: the two permutation indices are pushed as ordinary stack args (no this).
    // Beyond binding the two shader handles, the native bind also advances a per-draw fog/alpha state
    // machine, so the detour runs the original (preserving that state) and binds its own handles over it.
    using EffectBindFn = void(__cdecl*)(uint32_t vtxIdx, uint32_t pixIdx);
}
