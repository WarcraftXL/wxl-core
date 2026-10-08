// Overlay renderer API: lets a device extension draw the core's ImGui overlay with its own GPU API.
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

#ifndef WXL_OVERLAY_API_H
#define WXL_OVERLAY_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WXL_OVERLAY_API_NAME "wxl.ui.overlay"
#define WXL_OVERLAY_API_VERSION 1

/*
 * With the stock D3D9 device the core draws its overlay through the IDirect3DDevice9 and nothing here
 * is used. A device backend that creates no IDirect3DDevice9 registers a renderer instead: the core
 * still runs the overlay (input, panels, the ImGui frame) on its backend-neutral frame event, and hands
 * the finished frame to the renderer as plain arrays, so the renderer needs no ImGui headers and no
 * C++ ABI shared with the core.
 *
 * Everything runs on the render thread, inside the engine's GxScenePresent, before the backend's
 * ScenePresent slot: the frame is complete and still open on the back buffer.
 */

/**
 * One vertex, 20 bytes: position in display pixels (float2), texture coordinate (float2), colour as
 * four bytes R, G, B, A in memory order (R8G8B8A8_UNORM).
 */
typedef struct WXL_OverlayVertex
{
    float    pos[2];
    float    uv[2];
    uint32_t rgba;
} WXL_OverlayVertex;

/**
 * One draw: `elemCount` 16-bit indices from `idxOffset` of the list's index array, each added to
 * `vtxOffset` to address the list's vertex array. `clip` is {minX, minY, maxX, maxY} in display pixels
 * (the frame's displayPos at the origin); `texture` is a handle CreateTexture returned.
 */
typedef struct WXL_OverlayCmd
{
    float    clip[4];
    void*    texture;
    uint32_t vtxOffset;
    uint32_t idxOffset;
    uint32_t elemCount;
} WXL_OverlayCmd;

/** One draw list: its vertices and indices, and the draws over them in order. */
typedef struct WXL_OverlayList
{
    const WXL_OverlayVertex* vertices;
    uint32_t                 vertexCount;
    const uint16_t*          indices;
    uint32_t                 indexCount;
    const WXL_OverlayCmd*    cmds;
    uint32_t                 cmdCount;
} WXL_OverlayList;

/**
 * One overlay frame. The display is the client window in pixels: a renderer whose back buffer has
 * another size maps [displayPos, displayPos + displaySize] onto the whole of it, and scales the clip
 * rectangles by the same ratio.
 */
typedef struct WXL_OverlayFrame
{
    float                  displayPos[2];
    float                  displaySize[2];
    uint32_t               totalVertices;
    uint32_t               totalIndices;
    const WXL_OverlayList* lists;
    uint32_t               listCount;
} WXL_OverlayFrame;

/** What a device backend supplies. Copied at registration. */
typedef struct WXL_OverlayRenderer
{
    uint32_t structSize;
    void*    user;

    /// A sampled RGBA8 texture of width x height, rows tightly packed (the font atlas). May be
    /// called from inside the frame. Returns an opaque non-null handle, or NULL on failure.
    void* (__cdecl* CreateTexture)(void* user, const uint8_t* rgba, uint32_t width, uint32_t height);

    /// Destroys a texture CreateTexture returned (when the renderer is withdrawn).
    void (__cdecl* DestroyTexture)(void* user, void* texture);

    /// Draws one finished overlay frame on top of the back buffer. Alpha-blended (src alpha,
    /// 1 - src alpha), no depth, no culling, a scissor per draw.
    void (__cdecl* Render)(void* user, const WXL_OverlayFrame* frame);
} WXL_OverlayRenderer;

typedef struct WXL_OverlayApi
{
    uint32_t structSize;
    uint32_t apiVersion;

    /// Registers the renderer the overlay uses when no IDirect3DDevice9 exists. One at a time: a
    /// second registration replaces the first. Returns 1 on success.
    int (__cdecl* RegisterRenderer)(const WXL_OverlayRenderer* renderer, const char* name);

    /// Withdraws the registered renderer when it is @p renderer's (matched by its user pointer).
    void (__cdecl* UnregisterRenderer)(const WXL_OverlayRenderer* renderer);

    /// 1 while the overlay is open (F9) and drawing.
    int (__cdecl* IsOpen)(void);
} WXL_OverlayApi;

#ifdef __cplusplus
}
#endif

#endif // WXL_OVERLAY_API_H
