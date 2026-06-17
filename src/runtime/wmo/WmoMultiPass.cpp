// WMO multi-pass adapter: compose dropped layer textures (shine / emissive) over the native base draw.
// Copyright (C) 2026 WraithEngine
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

#include "WmoMultiPass.hpp"

#include "MultiPass.hpp"
#include "WMO.hpp"
#include "Gx.hpp"
#include "ChunkIO.hpp"
#include "WMO/WmoPassPlan.hpp"
#include "Logger.hpp"

#include <windows.h>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace off = wraith::offsets::game::wmo;
namespace gx  = wraith::offsets::engine::gx;
namespace mp  = wraith::runtime::multipass;
namespace sw  = wraith::structure::wmo;

namespace wraith::runtime::wmo::multipass
{
    namespace
    {
        // One cached extra layer for one material. Resolved lazily on first draw and held for the session.
        struct Layer
        {
            uint16_t   materialIndex;
            mp::Blend  blend;
            std::string path;
            void*      handle = nullptr; // resolved bindable texture handle
            bool       tried  = false;   // resolve attempted (success or fail)
        };

        // Plans keyed by normalized WMO root path.
        std::unordered_map<std::string, std::vector<Layer>> g_plans;

        // Lowercase + backslash, matching the key the loader stores at root+0x1c.
        void Normalize(char* s)
        {
            for (; *s; ++s)
                *s = (*s == '/') ? '\\' : static_cast<char>(tolower(static_cast<unsigned char>(*s)));
        }

        bool EndsWithCI(const char* s, const char* suffix)
        {
            const size_t ls = strlen(s), lf = strlen(suffix);
            if (lf > ls) return false;
            for (size_t i = 0; i < lf; ++i)
                if (tolower(static_cast<unsigned char>(s[ls - lf + i])) != suffix[i]) return false;
            return true;
        }

        mp::Blend ToMpBlend(sw::PassBlend b)
        {
            return (b == sw::PassBlend::Modulate) ? mp::Blend::Modulate : mp::Blend::Add;
        }

        // Copy the root path string (root+0x1c) into dst under SEH; false on a fault.
        bool SafeRootKey(int groupObj, char* dst, size_t cap)
        {
            __try
            {
                void* root = *reinterpret_cast<void**>(reinterpret_cast<char*>(groupObj) + off::kOffGroupRoot);
                const char* name = reinterpret_cast<const char*>(static_cast<char*>(root) + off::kOffNameInline);
                size_t i = 0;
                for (; i + 1 < cap && name[i]; ++i)
                    dst[i] = name[i];
                dst[i] = '\0';
                return i != 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // Replay the extra passes for every drawn batch of this group. Native-only body, SEH-guarded.
        void DrawOverlays(int groupObj, std::vector<Layer>& layers)
        {
            __try
            {
                void* device = *reinterpret_cast<void**>(gx::kDevicePtr);
                if (!device) return;
                if (*reinterpret_cast<int*>(static_cast<char*>(device) + gx::kOffDeviceReady) == 0)
                    return;

                char* grp  = reinterpret_cast<char*>(groupObj);
                char* moba = *reinterpret_cast<char**>(grp + off::kOffGroupMobaPtr);
                const uint16_t n = *reinterpret_cast<uint16_t*>(grp + off::kOffGroupExtBatchCount);
                if (!moba || n == 0) return;

                void** vt = *reinterpret_cast<void***>(device);
                auto submit = reinterpret_cast<gx::Gx_SubmitFn>(vt[gx::kVtSubmitByte / 4]);
                auto loadTex = reinterpret_cast<off::Wmo_LoadTextureByNameFn>(off::kLoadTextureByName);
                auto setState = reinterpret_cast<gx::Gx_SetDeviceStateFn>(gx::kSetDeviceState);

                for (size_t k = 0; k < layers.size(); ++k)
                {
                    Layer& L = layers[k];
                    if (L.tried)
                        continue;
                    L.tried = true;
                    void* texObj = loadTex(L.path.c_str());
                    L.handle = texObj ? mp::ResolveTexture(texObj, false) : nullptr;
                }

                setState(gx::kStateZWrite, 0);
                __try
                {
                    for (uint16_t i = 0; i < n; ++i)
                    {
                        char* b = moba + i * off::kMobaStride;
                        if ((*reinterpret_cast<uint8_t*>(b + off::kOffMobaDrawnFlag) & 0xF0) == 0)
                            continue; // not drawn this frame

                        const uint8_t matId = *reinterpret_cast<uint8_t*>(b + off::kOffMobaMatId);

                        // The batch geometry is the same for every extra pass; build the descriptor once.
                        uint8_t desc[0x10];
                        *reinterpret_cast<uint32_t*>(desc + 0x00) = 3; // tri-list
                        *reinterpret_cast<uint32_t*>(desc + 0x04) = *reinterpret_cast<uint32_t*>(b + off::kOffMobaStartIndex);
                        *reinterpret_cast<uint32_t*>(desc + 0x08) = *reinterpret_cast<uint16_t*>(b + off::kOffMobaIndexCount);
                        *reinterpret_cast<uint16_t*>(desc + 0x0C) = *reinterpret_cast<uint16_t*>(b + off::kOffMobaMinIndex);
                        *reinterpret_cast<uint16_t*>(desc + 0x0E) = *reinterpret_cast<uint16_t*>(b + off::kOffMobaMaxIndex);

                        for (size_t k = 0; k < layers.size(); ++k)
                        {
                            Layer& L = layers[k];
                            if (L.materialIndex != matId || !L.handle)
                                continue;
                            mp::SetBlend(L.blend);
                            mp::BindTexture(0, L.handle);
                            submit(device, desc, 1);
                        }
                    }
                }
                __finally
                {
                    mp::SetBlend(mp::Blend::Opaque);
                    setState(gx::kStateZWrite, 1);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                static int s_log = 0;
                if (s_log < 20) { ++s_log; WLOG_INFO("WMO multipass: overlay draw caught"); }
            }
        }
    }

    uint32_t IngestWmoBytes(const char* name, const uint8_t* buffer, uint32_t size)
    {
        if (!name || !buffer || size < 8 || !EndsWithCI(name, ".wmo"))
            return size;

        wraith::structure::iff::Chunk plan{};
        wraith::structure::iff::Reader reader(std::span<const uint8_t>(buffer, size));
        if (!reader.Find(sw::kWMP1, plan))
            return size;

        std::vector<sw::PassPlanEntry> entries;
        if (!sw::ParsePassPlan(plan.data, plan.size, entries) || entries.empty())
            return plan.pos - 8; // malformed plan: still hide the chunk

        std::vector<Layer> layers;
        layers.reserve(entries.size());
        for (sw::PassPlanEntry& e : entries)
            layers.push_back({ e.materialIndex, ToMpBlend(e.blend), std::move(e.texturePath), nullptr, false });

        char key[260];
        size_t i = 0;
        for (; i + 1 < sizeof(key) && name[i]; ++i) key[i] = name[i];
        key[i] = '\0';
        Normalize(key);
        g_plans[key] = std::move(layers);

        if (g_plans.size() <= 40)
            WLOG_INFO("WMO multipass: plan for '%s' (%u layers)", name, static_cast<unsigned>(entries.size()));

        return plan.pos - 8; // serve only the WMO bytes; hide WMP1 from the native loader
    }

    void RunExtraPasses(int groupObj)
    {
        char key[260];
        if (!SafeRootKey(groupObj, key, sizeof(key)))
            return;
        Normalize(key);

        auto it = g_plans.find(key);
        if (it == g_plans.end() || it->second.empty())
            return;

        DrawOverlays(groupObj, it->second);
    }
}
