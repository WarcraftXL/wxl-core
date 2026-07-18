// UI texture objects: load a .blp through game I/O, decode it, and own an IDirect3DTexture9 for ImGui.
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
#include <string>

#include "imgui.h" // ImTextureID

struct IDirect3DTexture9;

/// A UI texture: a decoded .blp uploaded to a live-device IDirect3DTexture9 that ImGui can sample. Textures
/// are loaded through the game's own file I/O (so MPQ-archived paths resolve) and decoded by BlpDecode; the
/// D3D texture is created in D3DPOOL_MANAGED so it survives a device reset with no invalidate/recreate. A
/// path-keyed cache owns every Texture for the lifetime of the VM: LoadBlp interns, ReleaseAll frees them
/// all at VM stop. All calls happen on the game/render thread inside the VM, so no locking is needed.
namespace wxl::lua::ui
{
    /// One cached texture: the device object plus its decoded dimensions and the source path (for display).
    /// Owned by the loader's cache; never freed via a Lua __gc.
    struct Texture
    {
        IDirect3DTexture9* tex    = nullptr;
        int                width  = 0;
        int                height = 0;
        std::string        path;
    };

    /**
     * @brief Loads (or returns the cached) texture for a .blp path.
     *
     * SEH-guarded across every game/device boundary: reads the bytes via the client's archive I/O, decodes
     * mip 0 to BGRA, then creates a managed IDirect3DTexture9 from the live device and uploads the pixels.
     * The same normalized path is loaded once and reused.
     *
     * @param path  the .blp path, e.g. "Interface\\Icons\\INV_Misc_QuestionMark.blp".
     * @return the cached Texture (owned by the cache), or nullptr on any failure (bad path, decode fail,
     *         no device, or CreateTexture fail).
     */
    Texture* LoadBlp(const char* path);

    /**
     * @brief Releases every cached texture and clears the cache. Call once on VM stop.
     *
     * After this returns every Texture* previously handed out is dangling, which is safe because the Lua
     * state that held the handles is being closed in the same teardown.
     */
    void ReleaseAll();

    /**
     * @brief The ImGui texture id for a Texture (the raw device pointer as an integer), 0 when null.
     * @param t  the texture (may be null).
     * @return the ImTextureID the DX9 backend expects.
     */
    inline ImTextureID TexId(const Texture* t)
    {
        return (t && t->tex) ? static_cast<ImTextureID>(reinterpret_cast<intptr_t>(t->tex))
                             : static_cast<ImTextureID>(0);
    }
}
