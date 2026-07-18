// m2 host face: register the byte-transform so a source M2 is reshaped on the host before serve.
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
#include "mpq/MpqStore.hpp"

#include "EquipmentFixes.hpp"

#include "engine/assets/shared/common/Text.hpp"
#include "engine/assets/shared/models/m2/Downport.hpp"
#include "engine/assets/shared/models/m2/Md21.hpp"
#include "engine/assets/shared/models/m2/Skel.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief The module's host face: plugs the shared byte-transform into the host's Transform hook.
 *
 * The DLL and the host run the same downport code, so a model the host reshapes is byte-for-byte what the
 * in-process DLL fallback would produce. The live-engine half (skin rebuild, draw fixups) has no host form and
 * stays in the DLL.
 */
namespace
{
    namespace dp   = wxl::modern::assets::m2::downport;
    namespace equip = wxl::modern::assets::m2::equipment;
    namespace m21  = wxl::modern::assets::m2::md21;
    namespace skel = wxl::modern::assets::m2::skel;
    namespace text = wxl::modern::assets::common::text;

    // Module-owned archive mount for the .skel sibling read; see AdtHost.cpp for why this is thread_local
    // (one StormLib handle per host worker thread) rather than a shared instance under a lock.
    thread_local wxl::host::mpq::MpqStore g_store;
    thread_local bool g_mounted = false;
    thread_local bool g_mountTried = false;

    bool EnsureMounted()
    {
        if (g_mounted) return true;
        if (g_mountTried) return false;
        g_mountTried = true;
        const std::string root = wxl::host::ClientRoot();
        g_mounted = !root.empty() && g_store.Mount(root);
        return g_mounted;
    }

    /**
     * @brief FileDataID -> path adapter for the MD21 de-chunk: routes to the host resolver and marks a
     *        resolved path as modern-sourced (TXID always resolves a texture, never a model) so BlpHost.cpp
     *        can scope its size cap to textures a modern M2 actually references, leaving native content
     *        untouched.
     * @param fileDataId  the id to resolve
     * @param outPath     receives the resolved path
     * @return true if the id resolved
     */
    bool ResolveThunk(uint32_t fileDataId, std::string& outPath)
    {
        if (!wxl::host::ResolveFdid(fileDataId, outPath)) return false;
        wxl::host::MarkModernTexture(outPath);
        return true;
    }

    /**
     * @brief Host Transform hook: reshapes a source M2 onto the client contract.
     * @param name Asset name; used to find a .skel sibling for a split-skeleton source model.
     * @param raw  Source asset bytes.
     * @param out  Receives the reshaped image on success.
     * @return true if the asset was reshaped; false (pass) for anything that is not a source image, so the
     *         host serves it raw.
     */
    bool Transform(std::string_view name, std::span<const uint8_t> raw, std::vector<uint8_t>& out)
    {
        const uint32_t size = static_cast<uint32_t>(raw.size());

        if (m21::IsMd21(raw))
        {
            std::vector<uint8_t> md20;
            if (!m21::Dechunk(raw, &ResolveThunk, md20)) return false;

            // A split-skeleton source (bones/sequences/attachments moved out of the MD20 body) ships a
            // sibling .skel file next to the .m2; splice it back in before the downport runs so bones and
            // sequences reach it and the client's bone-budget split at skin finalize.
            if (text::EndsWithCI(name, ".m2") && EnsureMounted())
            {
                std::string skelName(name.substr(0, name.size() - 3));
                skelName += ".skel";
                std::vector<uint8_t> skelBytes;
                if (g_store.ReadAll(skelName, skelBytes))
                    skel::Merge(skelBytes, md20);
            }

            const uint32_t orig = static_cast<uint32_t>(md20.size());
            const uint32_t work = dp::WorkSize(md20.data(), orig, name);
            md20.resize(work);
            if (!dp::ProcessInPlace(md20.data(), orig, work, name)) return false;
            m21::ZeroBoneLookup(md20.data(), static_cast<uint32_t>(md20.size()));
            equip::FixObjectSkinTextureTypes(name, md20);
            equip::ApplyHelmOffset(name, md20);
            out = std::move(md20);
            return true;
        }

        if (!dp::IsConvertible(raw.data(), size)) return false;

        const uint32_t workSize = dp::WorkSize(raw.data(), size, name);
        out.resize(workSize);
        std::memcpy(out.data(), raw.data(), size);
        if (!dp::ProcessInPlace(out.data(), size, workSize, name)) { out.clear(); return false; }
        equip::FixObjectSkinTextureTypes(name, out);
        equip::ApplyHelmOffset(name, out);
        return true;
    }

    /**
     * @brief File-scope registrar that self-registers the Transform hook at host startup, before main mounts
     *        the archives.
     */
    struct Registrar
    {
        Registrar()
        {
            // Install the host M2 transform only when the modern-M2 format is compiled in; disabled, the registrar is inert.
            if constexpr (wxl::features::kModernM2)
                wxl::host::RegisterTransform("modern-m2", &Transform);
        }
    };
    Registrar g_registrar;
}
