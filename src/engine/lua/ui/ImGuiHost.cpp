// ImGui host: the Dear ImGui lifecycle (context, backends, per-frame pump), decoupled from Lua.
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

#include "engine/lua/ui/ImGuiHost.hpp"

#include "engine/events/Event.hpp"
#include "common/Log.hpp"

#include <windows.h>
#include <d3d9.h>

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

// The Win32 backend leaves its message handler commented out (its header refuses to pull in <windows.h>),
// so we forward-declare it here, after <windows.h>, exactly as the backend documents.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace wxl::lua::ui
{
    namespace
    {
        namespace ev = wxl::events;

        bool g_installed = false; // Install() ran (events subscribed)
        bool g_ready     = false; // context + backends created (lazy, first frame)
        bool g_frameOpen = false; // NewFrame issued, matching Render still pending
        bool g_inDraw    = false; // inside the OnUiDraw emission -> wxl.ui.* is legal
        uint64_t g_frameGeneration = 0; // changes before every OnUiDraw emission
        HWND g_hwnd      = nullptr;

        /// Resolves the window the D3D9 device was created against. This is the reliable hwnd source: the
        /// focus window is fixed for the device's lifetime and is the one whose messages the input feature
        /// forwards, so ImGui's Win32 backend and its capture logic act on the very window the game uses.
        HWND FocusWindow(IDirect3DDevice9* device)
        {
            D3DDEVICE_CREATION_PARAMETERS params{};
            if (device->GetCreationParameters(&params) != D3D_OK)
                return nullptr;
            return params.hFocusWindow;
        }

        /// Creates the ImGui context and the Win32 + DX9 backends once the device is live. Idempotent.
        void EnsureReady(IDirect3DDevice9* device)
        {
            if (g_ready || !device)
                return;

            g_hwnd = FocusWindow(device);
            if (!g_hwnd)
            {
                WLOG_WARN("[ui] device has no focus window yet, deferring ImGui init");
                return;
            }

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGui::StyleColorsDark();

            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr; // no imgui.ini on disk; extensions own their layout

            const bool win32Ready = ImGui_ImplWin32_Init(g_hwnd);
            const bool dx9Ready = win32Ready && ImGui_ImplDX9_Init(device);
            if (!win32Ready || !dx9Ready)
            {
                WLOG_ERROR("[ui] ImGui backend init failed");
                if (dx9Ready) ImGui_ImplDX9_Shutdown();
                if (win32Ready) ImGui_ImplWin32_Shutdown();
                ImGui::DestroyContext();
                g_hwnd = nullptr;
                return;
            }

            g_ready = true;
            WLOG_DEBUG("[ui] ImGui ready (hwnd=%p)", g_hwnd);
        }

        /// OnFrame (fired at Present): lazily init, open a new ImGui frame, then hand the open frame to Lua
        /// via OnUiDraw. The matching Render happens at the next OnEndScene.
        void OnFrame(void* /*user*/, const void* args)
        {
            auto* device = static_cast<IDirect3DDevice9*>(static_cast<const ev::FrameArgs*>(args)->device);
            EnsureReady(device);
            if (!g_ready)
                return;

            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            g_frameOpen = true;
            if (++g_frameGeneration == 0) ++g_frameGeneration; // reserve zero for never-issued handles

            // The wxl.ui.* window: scripts registered on the 'draw' event build their UI now. InFrame()
            // is true only for the span of this emission, so a stray wxl.ui.* call elsewhere is rejected.
            g_inDraw = true;
            ev::UiDrawArgs draw{};
            ev::Emit(ev::Event::OnUiDraw, &draw);
            g_inDraw = false;
        }

        /// OnEndScene (fired just before the native EndScene, while the scene is still open): close the
        /// frame opened at the previous Present and issue the draw. EndScene precedes Present within a
        /// frame, so the UI built at frame N's Present is rendered at frame N+1's EndScene -- one matched
        /// NewFrame/Render pair per cycle, with a one-frame display latency.
        void OnEndScene(void* /*user*/, const void* /*args*/)
        {
            if (!g_ready || !g_frameOpen)
                return;

            ImGui::EndFrame();
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_frameOpen = false;
        }

        /// Before the device Reset: release every D3DPOOL_DEFAULT resource the DX9 backend holds. A frame
        /// opened this cycle is abandoned (its resources are about to die); the next Present reopens one.
        void OnDeviceLost(void* /*user*/, const void* /*args*/)
        {
            if (!g_ready)
                return;
            if (g_frameOpen)
            {
                ImGui::EndFrame();
                g_frameOpen = false;
                g_inDraw = false;
            }
            ImGui_ImplDX9_InvalidateDeviceObjects();
        }

        /// After a successful device Reset: recreate the backend's device objects. The font atlas and
        /// buffers are rebuilt lazily by the next NewFrame too, but recreating here matches the contract.
        void OnDeviceReset(void* /*user*/, const void* /*args*/)
        {
            if (!g_ready)
                return;
            ImGui_ImplDX9_CreateDeviceObjects();
        }

        /// OnInput: forward the window message to ImGui, then swallow it when ImGui owns that input class,
        /// so the game does not also react to a click/keypress meant for a widget.
        void OnInput(void* /*user*/, const void* args)
        {
            if (!g_ready)
                return;
            const auto* in = static_cast<const ev::InputArgs*>(args);

            ImGui_ImplWin32_WndProcHandler(g_hwnd,
                                           static_cast<UINT>(in->message),
                                           static_cast<WPARAM>(in->wparam),
                                           static_cast<LPARAM>(in->lparam));

            const ImGuiIO& io = ImGui::GetIO();
            const UINT     m  = static_cast<UINT>(in->message);
            const bool isMouse = (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST);
            const bool isKey   = (m >= WM_KEYFIRST   && m <= WM_KEYLAST);

            if ((isMouse && io.WantCaptureMouse) || (isKey && io.WantCaptureKeyboard))
                *in->handled = true; // swallow: the game skips this message (see InputArgs)
        }
    } // namespace

    void Install()
    {
        if (g_installed)
            return;
        g_installed = true;

        ev::Subscribe(ev::Event::OnFrame,       &OnFrame,       nullptr);
        ev::Subscribe(ev::Event::OnEndScene,    &OnEndScene,    nullptr);
        ev::Subscribe(ev::Event::OnDeviceLost,  &OnDeviceLost,  nullptr);
        ev::Subscribe(ev::Event::OnDeviceReset, &OnDeviceReset, nullptr);
        ev::Subscribe(ev::Event::OnInput,       &OnInput,       nullptr);

        WLOG_DEBUG("[ui] ImGui host installed; context boots on the first frame");
    }

    bool InFrame()
    {
        return g_inDraw;
    }

    uint64_t FrameGeneration()
    {
        return g_frameGeneration;
    }
}
