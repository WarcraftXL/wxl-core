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
    constexpr uintptr_t kNativeBlsLoad = 0x00684970;

    // --- per-shader D3D create seam (below the effect-collection stack) ---------------------------
    // CGxDeviceD3d::IShaderCreateVertex: the point where a CGxShader wrapper's bytecode becomes a live
    // IDirect3DVertexShader9. Reads the wrapper's bytecode pointer (+kCgxShaderBytePtr) and length
    // (+kCgxShaderByteLen), calls the device CreateVertexShader (D3D vtbl +0x16c), and stores the handle
    // at +kCgxShaderHandle. Confirmed __thiscall(device /*ecx*/, wrapper /*one stack arg*/), ret 4. Detour
    // as __fastcall(device, edx, wrapper) — byte-compatible — to substitute a recognised shader's bytecode
    // (swap the wrapper's +0x50/+0x4c fields around the original call, then restore).
    constexpr uintptr_t kShaderCreateVertex = 0x006AA0D0;
    using ShaderCreateVertexFn = void(__fastcall*)(void* device, void* edx, void* wrapper);

    // Global cdecl helper that uploads programmable-shader constants through the live device
    // (device vtbl +0x118, __thiscall): (target 0=vertex/4=pixel, startReg, const float* data, numVec4).
    // No-ops when data is null. The grass wind feature uses it to publish c35..c37 each frame.
    constexpr uintptr_t kShaderConstantsSet = 0x00408210;
    using ShaderConstantsSetHelperFn = void(__cdecl*)(int target, int startReg, const float* data, int numVec4);

    // --- selection-state globals (the live inputs the own stack reads instead of a positional slot) --
    constexpr uintptr_t kShadowTier          = 0x00D43010; // shadow tier, clamped 0..2
    constexpr uintptr_t kShadowGroup         = 0x00D43014; // pixel shadow group
    constexpr uintptr_t kProgrammablePathFlag = 0x00D43020; // master flag: programmable path active
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
    constexpr uintptr_t kGxStateSet      = 0x00685F50; // __thiscall(this=gx device, stateIdx, value)
    constexpr unsigned  kStateVertexShader = 0x4D;     // GxState slot the flush applies as vertex shader
    constexpr unsigned  kStatePixelShader  = 0x4E;     // GxState slot the flush applies as pixel shader
    using GxStateSetFn = void(__thiscall*)(void* gxDevice, unsigned stateIdx, void* value);

    // --- CGxShader wrapper layout (the native per-permutation shader object) -----------------------
    // The GxState flush applies a slot by reading the WRAPPER at the slot: if its created flag (+0x30) is
    // set it binds the live handle (+0x20) straight to the device, else it re-creates from the bytecode
    // pointer (+0x50)/length (+0x4c). The own stack therefore builds a minimal wrapper: live handle at
    // +0x20 and created flag = 1 at +0x30 (so the flush binds our handle and never tries to re-create).
    // Live handle at +0x20, created flag +0x30, bytecode length +0x4c, bytecode pointer +0x50.
    // RTTI ".?AVCGxShader@@".
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
