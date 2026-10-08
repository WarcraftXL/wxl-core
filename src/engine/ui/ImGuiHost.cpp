// In-game ImGui host: device lifetime, input routing, and a registry of panels to draw.
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
//
// MECHANISM
//   Everything this needs already exists as an event, so nothing here hooks anything: the overlay
//   builds and draws on OnEndScene (the frame is complete, the device is live, and drawing here
//   lands on top of the game's own UI), releases its device objects on OnDeviceLost and rebuilds
//   them on OnDeviceReset, and reads the keyboard and mouse through OnInput, which is swallowable.
//
//   THE ONE RULE WORTH KNOWING: the overlay only consumes input while it is OPEN. A debug overlay
//   that eats keystrokes when it is not visible is indistinguishable from a broken game, and the
//   report that comes back is never "your overlay is stealing input".
//
//   Initialisation is lazy and happens on the first OnEndScene, because the device does not exist
//   when features install. Everything degrades to "no overlay" rather than to a crash.
//
// WITHOUT AN IDirect3DDevice9
//   A device backend that creates none (wxl-vulkan-api's native device) gets OnEndScene from the
//   backend-neutral frame hook (client/CWorldScene/Render.cpp) with a null device. The DX9 path above
//   never sees that call -- it is the one with a device -- so it is exactly what it was. The null call
//   runs the same ImGui frame and hands the result, as the plain arrays of wxl/OverlayApi.h, to the
//   renderer the backend registered through "wxl.ui.overlay"; no renderer, no overlay. The Win32
//   platform backend follows the device window, which a native format change recreates. With no
//   panel registered this path shows a small built-in status window, since an ImGui frame with no
//   window has no vertex and the renderer would never be called (F9 would look dead).

#include "engine/hook/Registry.hpp"
#include "engine/events/Event.hpp"
#include "engine/ui/ImGuiHost.hpp"
#include "runtime/Extensions.hpp"
#include "wxl/OverlayApi.h"
#include "wxl/game/Binding.hpp"
#include "wxl/game/Gx.hpp"
#include "wxl/offsets/engine/GxDevice.hpp"

#include "common/Log.hpp"

#include <windows.h>
#include <d3d9.h>

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

#include <cstdint>
#include <cstring>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg,
                                                             WPARAM wParam, LPARAM lParam);

namespace
{
    namespace ev = wxl::events;

    struct Panel
    {
        const char*      title;
        wxl::ui::PanelFn fn;
        void*            user;
    };

    constexpr int kMaxPanels = 16;
    Panel g_panels[kMaxPanels]{};
    int   g_panelCount = 0;

    bool  g_ready   = false;   // backends initialised
    bool  g_failed  = false;   // initialisation failed once; do not retry every frame
    bool  g_open    = false;   // overlay visible and taking input
    HWND  g_hwnd    = nullptr;
    bool  g_native  = false;   // g_ready on the registered renderer, not on DX9

    // --- the renderer a backend without an IDirect3DDevice9 registers (wxl/OverlayApi.h) ---
    WXL_OverlayRenderer g_renderer{};
    bool  g_hasRenderer = false;
    char  g_rendererName[64] = "";
    void* g_fontTexture = nullptr;
    bool  g_nativeFailed = false;

    // The frame handed to the renderer, rebuilt each drawn frame; kept to reuse the allocations.
    std::vector<WXL_OverlayList> g_lists;
    std::vector<WXL_OverlayCmd>  g_cmds;

    // --- diagnostics of the null-device chain (one-shot lines, counters quoted by them) ---
    uint64_t g_nativeEndScenes = 0;   // OnEndScene calls with a null device
    uint64_t g_nativeBuilt     = 0;   // ImGui frames built for the renderer
    uint64_t g_nativeEmpty     = 0;   // built frames with no vertex, not handed over
    uint64_t g_nativeRendered  = 0;   // frames handed to the renderer's Render
    unsigned g_toggleLines     = 0;
    bool     g_saidFirstKey    = false;
    bool     g_saidFirstBuilt  = false;
    bool     g_saidFirstRender = false;
    bool     g_saidEmpty       = false;
    bool     g_saidNotReady    = false;

    static_assert(sizeof(ImDrawIdx) == 2, "wxl/OverlayApi.h hands 16-bit indices");
    static_assert(sizeof(ImDrawVert) == sizeof(WXL_OverlayVertex), "ImDrawVert is the API's vertex");

    /// The toggle. Chosen because the client binds neither, and because a key that needs a modifier
    /// is unreachable once the overlay is swallowing modifiers.
    constexpr int kToggleKey = VK_F9;

    HWND WindowOfDevice(IDirect3DDevice9* dev)
    {
        D3DDEVICE_CREATION_PARAMETERS cp{};
        if (SUCCEEDED(dev->GetCreationParameters(&cp)) && cp.hFocusWindow) return cp.hFocusWindow;
        return GetActiveWindow();
    }

    bool EnsureReady(IDirect3DDevice9* dev)
    {
        if (g_ready) return !g_native;   // g_native is never set while a D3D9 device draws
        if (g_failed || !dev) return false;

        g_hwnd = WindowOfDevice(dev);
        if (!g_hwnd) { g_failed = true; WLOG_WARN("imgui: no window, overlay disabled"); return false; }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        // No .ini: the overlay is a debugging surface, and a file that silently restores a window
        // dragged off-screen three sessions ago costs more than remembering layouts is worth.
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();

        if (!ImGui_ImplWin32_Init(g_hwnd) || !ImGui_ImplDX9_Init(dev))
        {
            g_failed = true;
            WLOG_WARN("imgui: backend init failed, overlay disabled");
            return false;
        }
        g_ready = true;
        WLOG_INFO("imgui: overlay ready (hwnd=%p) -- F9 toggles", g_hwnd);
        return true;
    }

    /// The engine device's window (+0x3968), which a format change replaces.
    HWND DeviceWindow()
    {
        void* gfx = wxl::game::gx::RawGraphicsDevice();
        return gfx ? wxl::game::At<HWND>(gfx, wxl::offsets::engine::gxdevice::kWindow) : nullptr;
    }

    /// Drops the native overlay's ImGui context and its font texture (renderer withdrawn).
    void ShutdownNative()
    {
        if (!g_ready || !g_native) return;
        if (g_fontTexture && g_hasRenderer && g_renderer.DestroyTexture)
            g_renderer.DestroyTexture(g_renderer.user, g_fontTexture);
        g_fontTexture = nullptr;
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_ready  = false;
        g_native = false;
        g_open   = false;
    }

    /**
     * The overlay's set-up when there is no IDirect3DDevice9: the same context and style as
     * EnsureReady, the Win32 backend on the device window, and the font atlas made a texture by the
     * registered renderer. Re-binds the Win32 backend when the device window changed.
     */
    bool EnsureNativeReady()
    {
        if (!g_hasRenderer || g_nativeFailed) return false;
        if (g_ready && !g_native) return false;   // the DX9 path owns the context
        const HWND hwnd = DeviceWindow();
        if (!hwnd) return false;

        if (g_ready)
        {
            if (hwnd != g_hwnd)
            {
                // A format change destroyed the window the backend was bound to.
                ImGui_ImplWin32_Shutdown();
                ImGui_ImplWin32_Init(hwnd);
                WLOG_INFO("imgui: overlay follows the device window %p -> %p", g_hwnd, hwnd);
                g_hwnd = hwnd;
            }
            return true;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;   // as EnsureReady
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();

        if (!ImGui_ImplWin32_Init(hwnd))
        {
            ImGui::DestroyContext();
            g_nativeFailed = true;
            WLOG_WARN("imgui: Win32 backend init failed, native overlay disabled");
            return false;
        }
        io.BackendRendererName = g_rendererName;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

        unsigned char* pixels = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        g_fontTexture = (pixels && w > 0 && h > 0 && g_renderer.CreateTexture)
                            ? g_renderer.CreateTexture(g_renderer.user, pixels, uint32_t(w), uint32_t(h))
                            : nullptr;
        if (!g_fontTexture)
        {
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            g_nativeFailed = true;
            WLOG_WARN("imgui: %s could not make the font texture (%dx%d), native overlay disabled",
                      g_rendererName, w, h);
            return false;
        }
        io.Fonts->SetTexID(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(g_fontTexture)));

        g_hwnd   = hwnd;
        g_ready  = true;
        g_native = true;
        WLOG_INFO("imgui: overlay ready on %s, no IDirect3DDevice9 (hwnd=%p, font %dx%d) -- F9 toggles",
                  g_rendererName, hwnd, w, h);
        return true;
    }

    /// ImGui's draw data as wxl/OverlayApi.h's arrays, handed to the renderer.
    void RenderNative(const ImDrawData* dd)
    {
        ++g_nativeBuilt;
        if (!g_saidFirstBuilt)
        {
            g_saidFirstBuilt = true;
            WLOG_INFO("imgui: first open overlay frame built on the null-device path: %d list(s), %d "
                      "vertices, %d indices, display %.0fx%.0f, %d panel(s) (null-device frame #%llu)",
                      dd ? dd->CmdListsCount : 0, dd ? dd->TotalVtxCount : 0, dd ? dd->TotalIdxCount : 0,
                      dd ? dd->DisplaySize.x : 0.0f, dd ? dd->DisplaySize.y : 0.0f, g_panelCount,
                      static_cast<unsigned long long>(g_nativeEndScenes));
        }
        if (!dd || dd->CmdListsCount <= 0 || dd->TotalVtxCount <= 0)
        {
            ++g_nativeEmpty;
            if (!g_saidEmpty)
            {
                g_saidEmpty = true;
                WLOG_WARN("imgui: the open overlay frame has nothing to draw (%d list(s), %d vertices); "
                          "the renderer is not called for it", dd ? dd->CmdListsCount : 0,
                          dd ? dd->TotalVtxCount : 0);
            }
            return;
        }
        g_lists.clear();
        g_cmds.clear();
        for (int n = 0; n < dd->CmdListsCount; ++n)
        {
            const ImDrawList* list = dd->CmdLists[n];
            for (const ImDrawCmd& c : list->CmdBuffer)
            {
                // No panel installs a callback; ImGui's own reset sentinel has nothing to reset here.
                if (c.UserCallback || c.ElemCount == 0) continue;
                WXL_OverlayCmd o{};
                o.clip[0] = c.ClipRect.x - dd->DisplayPos.x;
                o.clip[1] = c.ClipRect.y - dd->DisplayPos.y;
                o.clip[2] = c.ClipRect.z - dd->DisplayPos.x;
                o.clip[3] = c.ClipRect.w - dd->DisplayPos.y;
                o.texture   = reinterpret_cast<void*>(static_cast<uintptr_t>(c.GetTexID()));
                o.vtxOffset = c.VtxOffset;
                o.idxOffset = c.IdxOffset;
                o.elemCount = c.ElemCount;
                g_cmds.push_back(o);
            }
        }
        // Second walk, now that g_cmds no longer moves: each list points at its own run of draws.
        size_t at = 0;
        for (int n = 0; n < dd->CmdListsCount; ++n)
        {
            const ImDrawList* list = dd->CmdLists[n];
            size_t count = 0;
            for (const ImDrawCmd& c : list->CmdBuffer)
                if (!c.UserCallback && c.ElemCount != 0) ++count;
            WXL_OverlayList l{};
            l.vertices    = reinterpret_cast<const WXL_OverlayVertex*>(list->VtxBuffer.Data);
            l.vertexCount = uint32_t(list->VtxBuffer.Size);
            l.indices     = list->IdxBuffer.Data;
            l.indexCount  = uint32_t(list->IdxBuffer.Size);
            l.cmds        = count ? &g_cmds[at] : nullptr;
            l.cmdCount    = uint32_t(count);
            at += count;
            g_lists.push_back(l);
        }

        WXL_OverlayFrame f{};
        f.displayPos[0]  = dd->DisplayPos.x;
        f.displayPos[1]  = dd->DisplayPos.y;
        f.displaySize[0] = dd->DisplaySize.x;
        f.displaySize[1] = dd->DisplaySize.y;
        f.totalVertices  = uint32_t(dd->TotalVtxCount);
        f.totalIndices   = uint32_t(dd->TotalIdxCount);
        f.lists          = g_lists.data();
        f.listCount      = uint32_t(g_lists.size());
        g_renderer.Render(g_renderer.user, &f);
        ++g_nativeRendered;
        if (!g_saidFirstRender)
        {
            g_saidFirstRender = true;
            WLOG_INFO("imgui: first overlay frame handed to %s's Render: %u list(s), %zu draw(s), %u vertices, "
                      "%u indices", g_rendererName, f.listCount, g_cmds.size(), f.totalVertices,
                      f.totalIndices);
        }
    }

    /**
     * What the overlay shows while no extension registered a panel, on either path. Without it an
     * open overlay is an empty ImGui frame -- no window, no vertex -- and F9 looks dead.
     */
    void BuiltInPanel()
    {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("WarcraftXL"))
        {
            if (g_native)
                ImGui::Text("Renderer: %s (no IDirect3DDevice9)", g_rendererName);
            else
                ImGui::TextUnformatted("Renderer: Direct3D 9");
            ImGui::Text("%.1f fps (%.2f ms)", io.Framerate, io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
            ImGui::Text("Display %.0f x %.0f", io.DisplaySize.x, io.DisplaySize.y);
            if (g_native)
                ImGui::Text("Frames: %llu seen, %llu drawn", static_cast<unsigned long long>(g_nativeEndScenes),
                            static_cast<unsigned long long>(g_nativeRendered));
            ImGui::Separator();
            ImGui::TextUnformatted("No extension registered a panel. F9 closes.");
        }
        ImGui::End();
    }

    /// OnEndScene with no IDirect3DDevice9: the overlay frame for the registered renderer.
    void OnEndSceneNative()
    {
        ++g_nativeEndScenes;
        if (g_nativeEndScenes == 1000)
            WLOG_INFO("imgui: 1000 null-device OnEndScene calls reached the overlay host (ready=%d, open=%d, "
                      "native=%d)", g_ready ? 1 : 0, g_open ? 1 : 0, g_native ? 1 : 0);
        if (!EnsureNativeReady()) return;
        if (!g_open) return;

        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        for (int i = 0; i < g_panelCount; ++i)
        {
            if (ImGui::Begin(g_panels[i].title)) g_panels[i].fn(g_panels[i].user);
            ImGui::End();
        }
        if (g_panelCount == 0) BuiltInPanel();
        ImGui::EndFrame();
        ImGui::Render();
        RenderNative(ImGui::GetDrawData());
    }

    void OnEndScene(void*, const void* args)
    {
        auto* a = static_cast<const ev::EndSceneArgs*>(args);
        auto* dev = static_cast<IDirect3DDevice9*>(a->device);
        if (!dev)
        {
            OnEndSceneNative();
            return;
        }
        if (!EnsureReady(dev)) return;
        if (!g_open) return;

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        for (int i = 0; i < g_panelCount; ++i)
        {
            if (ImGui::Begin(g_panels[i].title)) g_panels[i].fn(g_panels[i].user);
            ImGui::End();
        }
        if (g_panelCount == 0) BuiltInPanel();

        ImGui::EndFrame();
        ImGui::Render();

        // The game leaves state set for whatever it drew last, and the DX9 backend assumes nothing.
        // A state block is the cheap way to be certain the overlay hands the device back untouched;
        // without it the first frame after the overlay opens can lose the game's own render states.
        IDirect3DStateBlock9* block = nullptr;
        if (SUCCEEDED(dev->CreateStateBlock(D3DSBT_ALL, &block)) && block)
        {
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            block->Apply();
            block->Release();
        }
        else
        {
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        }
    }

    void OnDeviceLost(void*, const void*)
    {
        if (g_ready && !g_native) ImGui_ImplDX9_InvalidateDeviceObjects();
    }

    void OnDeviceReset(void*, const void*)
    {
        if (g_ready && !g_native) ImGui_ImplDX9_CreateDeviceObjects();
    }

    // --- wxl.ui.overlay ----------------------------------------------------------------------------

    int __cdecl ApiRegisterRenderer(const WXL_OverlayRenderer* renderer, const char* name)
    {
        if (!renderer || renderer->structSize < sizeof(WXL_OverlayRenderer) || !renderer->CreateTexture
            || !renderer->Render)
            return 0;
        if (g_hasRenderer) ShutdownNative();   // the old renderer's font texture goes with it
        g_renderer    = *renderer;
        g_hasRenderer = true;
        g_nativeFailed = false;
        std::strncpy(g_rendererName, name && name[0] ? name : "unnamed", sizeof g_rendererName - 1);
        g_rendererName[sizeof g_rendererName - 1] = '\0';
        WLOG_INFO("imgui: overlay renderer registered by %s (used when no IDirect3DDevice9 exists)",
                  g_rendererName);
        return 1;
    }

    void __cdecl ApiUnregisterRenderer(const WXL_OverlayRenderer* renderer)
    {
        if (!renderer || !g_hasRenderer || renderer->user != g_renderer.user) return;
        ShutdownNative();
        g_hasRenderer = false;
        g_renderer    = WXL_OverlayRenderer{};
        WLOG_INFO("imgui: overlay renderer of %s withdrawn", g_rendererName);
    }

    int __cdecl ApiIsOpen() { return g_open ? 1 : 0; }

    const WXL_OverlayApi g_overlayApi = {
        sizeof(WXL_OverlayApi),
        WXL_OVERLAY_API_VERSION,
        &ApiRegisterRenderer,
        &ApiUnregisterRenderer,
        &ApiIsOpen,
    };

    /// Boot phase, before any extension loads, as wxl.graphics.device: a device extension asks for it
    /// from its own load or its device create, both earlier than the Normal-phase install below.
    bool InstallOverlayApi()
    {
        wxl::runtime::extensions::PublishInterface(WXL_OVERLAY_API_NAME, WXL_OVERLAY_API_VERSION,
                                                   const_cast<WXL_OverlayApi*>(&g_overlayApi));
        return true;
    }

    void OnInput(void*, const void* args)
    {
        auto* a = const_cast<ev::InputArgs*>(static_cast<const ev::InputArgs*>(args));

        // Diagnostics only: proves the subclass delivers keyboard messages at all.
        if (!g_saidFirstKey && (a->message == WM_KEYDOWN || a->message == WM_KEYUP
                                || a->message == WM_SYSKEYDOWN || a->message == WM_SYSKEYUP))
        {
            g_saidFirstKey = true;
            WLOG_INFO("imgui: first keyboard message reached OnInput (msg 0x%04X, vk 0x%02X)",
                      a->message, static_cast<unsigned>(a->wparam));
        }

        // The toggle is read before anything else and never forwarded, so the key cannot also reach
        // the game. Handled on key-UP: a key-DOWN repeats while held and the overlay would strobe.
        if (a->message == WM_KEYUP && static_cast<int>(a->wparam) == kToggleKey)
        {
            if (g_ready)
            {
                g_open = !g_open;
                // The client hides and clips the cursor for mouselook. Releasing the clip is what
                // makes the overlay actually usable; the client re-establishes it on its own once
                // the player moves the camera again, so nothing has to be restored here.
                if (g_open) ClipCursor(nullptr);
                if (g_toggleLines < 8)
                {
                    ++g_toggleLines;
                    WLOG_INFO("imgui: F9 -> overlay %s (%s path, %d panel(s); null-device frames: %llu seen, "
                              "%llu built, %llu empty, %llu rendered)%s",
                              g_open ? "open" : "closed", g_native ? "null-device" : "D3D9", g_panelCount,
                              static_cast<unsigned long long>(g_nativeEndScenes),
                              static_cast<unsigned long long>(g_nativeBuilt),
                              static_cast<unsigned long long>(g_nativeEmpty),
                              static_cast<unsigned long long>(g_nativeRendered),
                              g_toggleLines == 8 ? "; further toggles not logged" : "");
                }
            }
            else if (!g_saidNotReady)
            {
                g_saidNotReady = true;
                WLOG_WARN("imgui: F9 received but the overlay is not ready (failed=%d, native failed=%d, "
                          "renderer=%d, null-device frames seen %llu)", g_failed ? 1 : 0,
                          g_nativeFailed ? 1 : 0, g_hasRenderer ? 1 : 0,
                          static_cast<unsigned long long>(g_nativeEndScenes));
            }
            *a->handled = true;
            return;
        }

        if (!g_ready || !g_open) return;

        ImGui_ImplWin32_WndProcHandler(g_hwnd, a->message, static_cast<WPARAM>(a->wparam),
                                       static_cast<LPARAM>(a->lparam));

        // Swallow only what ImGui actually wants. Letting mouse messages through while a slider is
        // being dragged makes the camera spin under the panel; swallowing everything makes the game
        // unplayable with the overlay merely open.
        const ImGuiIO& io = ImGui::GetIO();
        const bool mouse = a->message >= WM_MOUSEFIRST && a->message <= WM_MOUSELAST;
        const bool keyb  = (a->message >= WM_KEYFIRST && a->message <= WM_KEYLAST);
        if ((mouse && io.WantCaptureMouse) || (keyb && io.WantCaptureKeyboard)) *a->handled = true;
    }

    bool InstallImGuiHost()
    {
        ev::Subscribe(ev::Event::OnEndScene,    &OnEndScene,    nullptr);
        ev::Subscribe(ev::Event::OnDeviceLost,  &OnDeviceLost,  nullptr);
        ev::Subscribe(ev::Event::OnDeviceReset, &OnDeviceReset, nullptr);
        ev::Subscribe(ev::Event::OnInput,       &OnInput,       nullptr);
        return true;
    }
}

namespace wxl::ui
{
    void AddPanel(const char* title, PanelFn fn, void* user)
    {
        if (g_panelCount >= kMaxPanels || !title || !fn) return;
        g_panels[g_panelCount++] = Panel{ title, fn, user };
    }

    bool IsOpen() { return g_open; }

    // A panel body runs between NewFrame and Render and inside an open window, so these need no
    // guard of their own beyond the null checks: the host only ever calls a body from there.
    namespace c
    {
        void __cdecl AddPanel(const char* title, void(__cdecl* fn)(void*), void* user)
        { wxl::ui::AddPanel(title, fn, user); }

        int __cdecl IsOpen() { return g_open ? 1 : 0; }

        void __cdecl Text(const char* text)
        { if (text) ImGui::TextUnformatted(text); }

        void __cdecl Separator() { ImGui::Separator(); }

        int __cdecl Button(const char* label)
        { return (label && ImGui::Button(label)) ? 1 : 0; }

        int __cdecl Checkbox(const char* label, int* value)
        {
            if (!label || !value) return 0;
            bool on = (*value != 0);
            const bool changed = ImGui::Checkbox(label, &on);
            if (changed) *value = on ? 1 : 0;
            return changed ? 1 : 0;
        }

        int __cdecl SliderFloat(const char* label, float* value, float min, float max)
        {
            if (!label || !value) return 0;
            return ImGui::SliderFloat(label, value, min, max) ? 1 : 0;
        }

        int __cdecl SliderInt(const char* label, int* value, int min, int max)
        {
            if (!label || !value) return 0;
            return ImGui::SliderInt(label, value, min, max) ? 1 : 0;
        }

        int __cdecl ColorEdit(const char* label, float rgba[4])
        {
            if (!label || !rgba) return 0;
            return ImGui::ColorEdit4(label, rgba) ? 1 : 0;
        }

        void __cdecl SameLine() { ImGui::SameLine(); }

        int __cdecl Combo(const char* label, int* index, const char* const* items, int count)
        {
            if (!label || !index || !items || count <= 0) return 0;
            // A caller's selection routinely outlives the list it was picked from -- the panel that
            // wants this walks one subject after another -- so an index outside the current list is
            // ordinary input and is brought back in range rather than refused.
            if (*index < 0) *index = 0;
            else if (*index >= count) *index = count - 1;
            return ImGui::Combo(label, index, items, count) ? 1 : 0;
        }

        int __cdecl CollapsingHeader(const char* label)
        { return (label && ImGui::CollapsingHeader(label)) ? 1 : 0; }

        int __cdecl InputText(const char* label, char* buf, size_t bufSize)
        {
            if (!label || !buf || bufSize == 0) return 0;
            return ImGui::InputText(label, buf, bufSize) ? 1 : 0;
        }
    }
}

// The overlay reads input only while open (F9), so it is always compiled in.
WXL_REGISTER_FEATURE("imgui-host", true, InstallImGuiHost)
WXL_REGISTER_FEATURE_PHASED("imgui-overlay-api", true, InstallOverlayApi, ::wxl::hook::Phase::Boot)
