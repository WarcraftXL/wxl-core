// M2 ground-shadow draw: diagnosis of, and in-place guard against, camera-locked shadow sections.
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

/**
 * @brief Why a modern-M2 ground shadow rotates with the camera, measured at the draw that renders it.
 *
 * The shadow-map vertex program comes in three variants, selected by
 * CShaderEffect::SetDefaultShaders(section->boneInfluences) clamped to 0..2. Only the variants for
 * boneInfluences >= 1 apply c14..c16 = inverse(cameraView) * lightView -- the rebase that cancels the
 * camera view carried by the c31 bone palette. The **boneInfluences == 0 variant goes straight from
 * the palette to the light projection**, so any section drawn through it is camera-locked by
 * construction: its shadow rotates rigidly with the camera, shape intact. That signature matches the
 * report exactly, whereas a merely stale palette would read as lag, not as rigid rotation.
 *
 * Modern skins do ship sections with boneInfluences == 0 (verified on disk: the ultratree's trunk).
 * Our live half is supposed to lift those to 1 at skin finalize (engine/assets/models/m2/Skin.cpp,
 * FixSubmeshes) BEFORE the stock finalize memcpys the runtime section copy to CM2Shared+0x18C. This
 * module checks that guarantee where it actually matters -- at the draw -- and repairs it in place if
 * it did not hold, because several links in that chain (rebuild gating, hot-reshaped registry state,
 * and the per-instance override arrays at CM2Model+0x2D0 that the rebuild never touches) have never
 * been observed at runtime.
 *
 * The repair writes the RUNTIME section copy, never the model file.
 */
namespace wxl::runtime::m2shadow
{
    struct Stats
    {
        uint32_t shadowDraws;      ///< shadow-map batch draws seen (0 => the hook never fires)
        uint32_t pairsLogged;      ///< distinct (model, section) pairs dumped to the log
        uint32_t influencesZero;   ///< draws whose section arrived with boneInfluences == 0
        uint32_t influencesFixed;  ///< of those, ones this module lifted to 1 in place
        uint32_t overrideSections; ///< draws whose section came from the CM2Model+0x2D0 override arrays
        uint32_t staleAnim;        ///< draws where the instance's last-animated frame != scene frame
        uint32_t paletteMismatch;  ///< draws where palette slot 0 != the per-frame view root
        uint32_t faults;           ///< SEH faults swallowed (the draw still ran untouched)
    };

    /**
     * @brief Records whether the probe has a live detour to ride.
     *
     * This module installs NO hook of its own: features/m2compat/Bones.cpp already detours
     * kRenderBatchShadowMap (0x00829BA0) for its oversized-palette guard, and MinHook rejects a second
     * detour on the same target with MH_ERROR_ALREADY_CREATED.
     */
    void Arm(bool hookInstalled);

    /**
     * @brief Called from the existing shadow-map draw detour, once per shadow batch, before the draw.
     *
     * Measures the four values that decide whether this draw can produce a stable shadow, and -- when
     * enabled -- lifts a zero boneInfluences to 1 so the draw takes a shader variant that applies the
     * camera-view rebase.
     * @param instance  the CM2Model being drawn (ECX of the stock function).
     * @param section   the M2SkinSection for this batch. Writable: it is a runtime copy.
     */
    void OnShadowBatch(void* instance, void* section);

    /** @brief True when the probe is compiled in AND the detour it rides installed successfully. */
    bool Installed();

    /** @brief Whether the in-place boneInfluences repair is active. Measurement runs either way. */
    bool Enabled();
    void SetEnabled(bool on);

    Stats GetStats();
}
