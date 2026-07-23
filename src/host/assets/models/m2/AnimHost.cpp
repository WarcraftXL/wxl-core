// Host face for m2 anim: strips the AFM2 chunk wrapper from a modern external animation file.
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
#include "Host.hpp"
#include "common/Log.hpp"
#include "engine/assets/m2/M2Format.hpp"

#include "engine/assets/shared/common/Env.hpp"
#include "engine/assets/shared/common/Text.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

// A modern (Legion+) M2 sequence whose data is streamed externally (M2Sequence flags bit 0x20 clear) ships
// its .anim file wrapped in a single AFM2 chunk: a 4-byte magic, a u32 payload size, then the payload -- the
// same flat legacy track-data blob the Client's external-animation loader expects with no header at all.
// This fires on every .anim open and strips the wrapper; a file already Client-shaped (no AFM2 magic) is
// served unchanged.
// A .skel-based model (wowdev M2 wiki, ".anim files" section) splits the same file into AFM2 (events only) +
// AFSA (attachments) + AFSB (bones) instead, which this does not unpack -- no served content ships a .skel
// sibling (checked across the archive), so that path is unreached, not silently mishandled.
namespace
{
    namespace fmt = wxl::structure::m2;
    namespace text = wxl::modern::assets::common::text;

    uint32_t Rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

    bool TransformAnim(std::string_view name, std::span<const uint8_t> raw, std::vector<uint8_t>& out)
    {
        if (!text::EndsWithCI(name, ".anim")) return false;
        if (raw.size() < 8 || Rd32(raw.data()) != fmt::kMagicAFM2) return false; // already Client-shaped

        const uint32_t payload = Rd32(raw.data() + 4);
        if (8 + size_t(payload) > raw.size()) return false;

        out.assign(raw.data() + 8, raw.data() + 8 + payload);
        if (wxl::modern::assets::common::env::VerboseAssetLogs())
            WLOG_INFO("modern-anim: %.*s unwrapped (%u -> %u bytes)",
                int(name.size()), name.data(), uint32_t(raw.size()), uint32_t(out.size()));
        return true;
    }

    // File-scope registrar: self-registers the transform before the host serve loop starts.
    struct Registrar
    {
        Registrar()
        {
            if constexpr (wxl::features::kModernAnim)
                wxl::host::RegisterTransform("modern-anim", &TransformAnim);
        }
    } g_registrar;
}
