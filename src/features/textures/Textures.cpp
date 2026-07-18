// Texture create/upload detours: publish BLP-load / texture-upload events and guard the mip-source singleton.
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

#include "config.hpp"
#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"
#include "engine/events/Event.hpp"
#include "features/diag/AssetProfile.hpp"

#include "common/Log.hpp"
#include "offsets/engine/Gx.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace
{
    namespace ev    = wxl::events;
    namespace gxoff = wxl::offsets::engine::gx;
    namespace aprof = wxl::runtime::assetprof;

    gxoff::TextureCreateFn g_origTexCreate = nullptr;
    gxoff::TextureUpdateFn g_origTexUpdate = nullptr;
    std::atomic<uint32_t>  g_textureUpdateFaults{ 0 };

    // Keep SEH in a POD-only leaf. TextureUpdate invokes the texture's completion callback before it
    // returns; a late font-atlas/cache callback can retain a row/tree pointer whose owner was rebuilt
    // during world entry. Letting that AV escape kills the client from TextureCallback (0x006C9F50).
    bool SafeTextureUpdate(void* tex, int x, int y, int x2, int y2, int flag) noexcept
    {
        __try
        {
            g_origTexUpdate(tex, x, y, x2, y2, flag);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    /**
     * @brief Detours texture upload, emitting OnTextureUpload before the device update.
     *
     * width=x2-x and height=y2-y cover both full-surface (x=y=0) and sub-rect uploads.
     *
     * The mip source the upload reads is a process-wide singleton (kMipTablePtr is a pointer whose
     * buffer holds the per-mip source pointers; kMipTableValid gates the read). A build fills it with
     * raw aliases into its transient IO buffer, then uploads. Two ways that singleton turns into an
     * access-violation use-after-free (0x40cb6a), both fixed without ever clearing kMipTableValid
     * (which would route atlas icons through a source callback that has no self-heal and blank them):
     *  - a NESTED build run while this upload is mid-copy overwrites the table and frees its buffer; the
     *    async-drain serializer (streaming) snapshots and restores the table around the nested build it
     *    runs, so the outer upload keeps reading its own live aliases.
     *  - a TRUNCATED mip chain (common in custom-map BLPs) only fills the low slots, so the dimension-
     *    driven upload would read a prior build's freed alias left in a high slot. Clearing the table
     *    after each upload leaves the next build's unfilled slots at 0, which the upload's source-not-null
     *    blit guard skips. Done after the original consumed the table, so the live upload is unaffected.
     * @param tex   texture being uploaded.
     * @param x     upload rect left.
     * @param y     upload rect top.
     * @param x2    upload rect right.
     * @param y2    upload rect bottom.
     * @param flag  native upload flag.
     */
    void __cdecl hkTexUpdate(void* tex, int x, int y, int x2, int y2, int flag)
    {
        ev::TextureUploadArgs a{ tex, static_cast<uint32_t>(x2 - x), static_cast<uint32_t>(y2 - y) };
        ev::Emit(ev::Event::OnTextureUpload, &a);
        const uint64_t started = aprof::Now();
        const bool completed = SafeTextureUpdate(tex, x, y, x2, y2, flag);
        if (!completed)
        {
            const uint32_t faults = g_textureUpdateFaults.fetch_add(1, std::memory_order_relaxed) + 1;
            if (faults == 1 || (faults & (faults - 1)) == 0)
                WLOG_WARN("texture: skipped stale native upload callback (faults=%u tex=%p rect=%d,%d..%d,%d)",
                          faults, tex, x, y, x2, y2);
        }
        else if (started)
        {
            const uint64_t width = x2 > x ? static_cast<uint64_t>(x2 - x) : 0;
            const uint64_t height = y2 > y ? static_cast<uint64_t>(y2 - y) : 0;
            aprof::Record(aprof::Phase::TextureUpload, aprof::Now() - started, width * height);
        }
        if (auto* tbl = *reinterpret_cast<uint32_t**>(gxoff::kMipTablePtr))
            std::memset(tbl, 0, gxoff::kMipTableSlots * sizeof(uint32_t));
    }

    /**
     * @brief Detours the by-name texture create, emitting OnBlpLoad after the request resolves.
     *
     * Fires on every reference (returns the cached handle on a hit), so the event carries the requested
     * name and a subscriber can watch for one specific BLP.
     * @param name    requested texture path (full virtual path).
     * @param flags   native load flags.
     * @param status  native status out-pointer.
     * @param flags2  native load flags.
     * @return the resolved texture handle (null on failure).
     */
    void* __cdecl hkTexCreate(const char* name, uint32_t flags, int* status, uint32_t flags2)
    {
        const uint64_t started = aprof::Now();
        void* handle = g_origTexCreate(name, flags, status, flags2);
        if (started) aprof::Record(aprof::Phase::TextureRequest, aprof::Now() - started);

        ev::BlpLoadArgs a{ name, handle };
        ev::Emit(ev::Event::OnBlpLoad, &a);

        return handle;
    }

    bool InstallTextures()
    {
        wxl::hook::Install("TextureUpdate", gxoff::kTextureUpdate, &hkTexUpdate, &g_origTexUpdate);
        wxl::hook::Install("TextureCreate", gxoff::kTextureCreate, &hkTexCreate, &g_origTexCreate);
        return true;
    }
}

WXL_REGISTER_FEATURE("textures", wxl::features::kTextures, InstallTextures)
