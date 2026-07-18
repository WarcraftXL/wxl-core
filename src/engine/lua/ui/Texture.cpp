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

#include "engine/lua/ui/Texture.hpp"

#include "engine/lua/ui/BlpDecode.hpp"
#include "game/gx/Gx.hpp"
#include "game/io/Io.hpp"

#include <windows.h>
#include <d3d9.h>

#include <cstring>
#include <unordered_map>
#include <vector>

namespace wxl::lua::ui
{
    namespace
    {
        namespace io = wxl::game::io;

        // Sanity cap so a corrupt size dword cannot ask for a multi-gigabyte allocation (UI art is small).
        constexpr uint32_t kMaxBlpBytes = 64u * 1024u * 1024u;

        // Path-keyed cache; the loader owns every Texture for the VM's lifetime. Single-threaded (game thread).
        std::unordered_map<std::string, Texture*> g_cache;

        // Case-insensitive, backslash-normalized key so "Interface/Icons/X.blp" and
        // "interface\\icons\\x.blp" resolve to the same cache entry.
        std::string NormalizeKey(const char* path)
        {
            std::string s(path);
            for (char& c : s)
            {
                if (c == '/') c = '\\';
                else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            }
            return s;
        }

        // --- SEH-guarded POD-only game/device boundaries (no C++ objects so __try can be used) ---------

        void* SafeOpen(const char* name)
        {
            __try
            {
                void* h = nullptr;
                if (io::FileOpen(name, wxl::offsets::engine::io::kOpenWholeFile, &h) && h)
                    return h;
                return nullptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
        }

        uint32_t SafeSize(void* h)
        {
            __try { return io::FileSize(h, nullptr); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        }

        bool SafeRead(void* h, void* dst, uint32_t len)
        {
            __try
            {
                uint32_t got = 0;
                return io::FileRead(h, dst, len, &got) != 0 && got == len;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        void SafeClose(void* h)
        {
            __try { io::FileClose(h); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // Creates a managed A8R8G8B8 texture from tightly packed BGRA and uploads it row by row (respecting
        // the locked pitch). Pure POD locals so the whole device sequence sits under one SEH guard.
        IDirect3DTexture9* SafeCreateTexture(const uint8_t* bgra, int w, int h)
        {
            __try
            {
                IDirect3DDevice9* dev = static_cast<IDirect3DDevice9*>(wxl::game::gx::RawDevice());
                if (!dev) return nullptr;

                IDirect3DTexture9* tex = nullptr;
                if (FAILED(dev->CreateTexture(static_cast<UINT>(w), static_cast<UINT>(h), 1, 0,
                                              D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) || !tex)
                    return nullptr;

                D3DLOCKED_RECT lr;
                if (FAILED(tex->LockRect(0, &lr, nullptr, 0)))
                {
                    tex->Release();
                    return nullptr;
                }
                uint8_t* dst = static_cast<uint8_t*>(lr.pBits);
                const size_t rowBytes = static_cast<size_t>(w) * 4;
                for (int y = 0; y < h; ++y)
                    std::memcpy(dst + static_cast<size_t>(y) * lr.Pitch, bgra + static_cast<size_t>(y) * rowBytes, rowBytes);
                tex->UnlockRect(0);
                return tex;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
        }

        void SafeRelease(IDirect3DTexture9* tex)
        {
            __try { if (tex) tex->Release(); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // Reads a whole game file into out. No __try here (out is an unwindable object); the raw boundary
        // calls are the SEH-guarded POD helpers above.
        bool ReadGameFile(const char* path, std::vector<uint8_t>& out)
        {
            void* h = SafeOpen(path);
            if (!h) return false;
            const uint32_t size = SafeSize(h);
            if (size == 0 || size > kMaxBlpBytes)
            {
                SafeClose(h);
                return false;
            }
            out.resize(size);
            const bool ok = SafeRead(h, out.data(), size);
            SafeClose(h);
            if (!ok)
            {
                out.clear();
                return false;
            }
            return true;
        }
    } // namespace

    Texture* LoadBlp(const char* path)
    {
        if (!path || !*path) return nullptr;

        const std::string key = NormalizeKey(path);
        const auto it = g_cache.find(key);
        if (it != g_cache.end())
            return it->second;

        std::vector<uint8_t> bytes;
        if (!ReadGameFile(path, bytes))
            return nullptr;

        int w = 0, h = 0;
        std::vector<uint8_t> bgra;
        if (!DecodeBlpToBgra(bytes.data(), bytes.size(), w, h, bgra))
            return nullptr;

        IDirect3DTexture9* d3d = SafeCreateTexture(bgra.data(), w, h);
        if (!d3d)
            return nullptr;

        Texture* t = new Texture();
        t->tex    = d3d;
        t->width  = w;
        t->height = h;
        t->path   = path;
        g_cache.emplace(key, t);
        return t;
    }

    void ReleaseAll()
    {
        for (auto& kv : g_cache)
        {
            if (kv.second)
            {
                SafeRelease(kv.second->tex);
                delete kv.second;
            }
        }
        g_cache.clear();
    }
}
