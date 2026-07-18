// wmo: in-process down-convert of a source WMO when the host did not already serve it.
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
#include "common/Log.hpp"
#include "engine/events/EventScript.hpp"
#include "game/wmo/Wmo.hpp"

#include "engine/assets/shared/models/wmo/WmoTranslate.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

// The live-engine half of the module. The host serves source WMOs already reshaped, but when the host is
// off (or did not serve a file) the bytes arrive source-shaped; the core fires OnWmoRootLoad/OnWmoGroupLoad
// after the buffer is read and before the native chunk walk, the window to reshape it in place. The same
// transform runs on the host: on an already-reshaped buffer it reports no change, so this is a no-op when
// the host already did the work.
namespace wxl::modern::assets::wmo
{
    namespace ev  = wxl::events;
    namespace gwmo = wxl::game::wmo;

    /**
     * @brief Binds the in-process down-convert to the core WMO load events.
     */
    class WmoRuntime : public ev::EventScript
    {
    public:
        WmoRuntime()
        {
            // Bind the WMO handlers only when the modern-WMO format is compiled in; disabled, the instance is inert.
            if constexpr (wxl::features::kModernWmo)
            {
                on<&WmoRuntime::OnRootLoad>(ev::Event::OnWmoRootLoad);
                on<&WmoRuntime::OnGroupLoad>(ev::Event::OnWmoGroupLoad);
                WLOG_INFO("modern-assets: wmo loaded (in-memory modern WMO asset support)");
            }
        }

    private:
        /**
         * @brief Reshapes a source WMO root in place before the native chunk walk.
         * @param a  Root load arguments carrying the map-object root.
         */
        void OnRootLoad(const ev::WmoRootLoadArgs& a)
        {
            void*          buf  = gwmo::RootBuffer(a.root);
            const uint32_t size = gwmo::RootSize(a.root);
            if (!buf || size < kMinWmo) return;

            std::vector<uint8_t> out;
            ResolveCtx rc{}; // source roots name their textures directly; no resolver needed
            if (!TranslateWmoRoot(std::span<const uint8_t>(static_cast<const uint8_t*>(buf), size), rc, out))
                return; // already Client-shaped (the host served it) or nothing to change

            if (!WriteBack(buf, size, out, "root")) return;
            gwmo::SetRootSize(a.root, static_cast<uint32_t>(out.size()));
        }

        /**
         * @brief Reshapes a source WMO group in place before the native sub-chunk walk.
         * @param a  Group load arguments carrying the map-object group.
         */
        void OnGroupLoad(const ev::WmoGroupLoadArgs& a)
        {
            void*          buf  = gwmo::GroupBuffer(a.group);
            const uint32_t size = gwmo::GroupSize(a.group);
            if (!buf || size < kMinWmo) return;

            std::vector<uint8_t> out;
            if (!TranslateWmoGroup(std::span<const uint8_t>(static_cast<const uint8_t*>(buf), size), out))
                return;

            if (!WriteBack(buf, size, out, "group")) return;
            gwmo::SetGroupSize(a.group, static_cast<uint32_t>(out.size()));
        }

        /**
         * @brief Copies the reshaped bytes back over the source buffer when they fit.
         * @param buf   destination buffer (the engine's read buffer).
         * @param size  destination capacity (the read byte size).
         * @param out   reshaped bytes.
         * @param what  "root" or "group", for the skip log.
         * @return true when the bytes were written, false when they would not fit.
         */
        bool WriteBack(void* buf, uint32_t size, const std::vector<uint8_t>& out, const char* what)
        {
            // The reshape is reading from `buf`, but `out` is a separate buffer, so the copy never overlaps
            // the read. A grown image cannot be written in place; reallocation is the host's job, so log and
            // leave the source bytes (the host path covers the grown case).
            if (out.size() > size)
            {
                WLOG_WARN("modern-wmo: %s grew %zu > %u, left for the host path", what, out.size(), size);
                return false;
            }
            std::memcpy(buf, out.data(), out.size());
            return true;
        }

        static constexpr uint32_t kMinWmo = 12; // MVER header + one chunk tag
    };

    // File-scope instance self-registers its handlers at DLL load via the EventScript ctor.
    WmoRuntime g_wmoRuntime;
}
