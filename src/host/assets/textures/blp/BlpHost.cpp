// Host face for wxl-modern-blp: serves an uncompressed-BGRA texture as a DXT5 texture the Client reads.
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

#include "engine/assets/shared/common/Env.hpp"
#include "engine/assets/shared/common/Text.hpp"
#include "engine/assets/shared/textures/blp/BlpTranscode.hpp"

#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Fires on every .blp open. A texture in the uncompressed-BGRA encoding (3) the Client cannot decode is
// re-encoded to DXT5; every other texture is served raw (the transcode declines fast after the header).
namespace
{
    namespace text = wxl::modern::assets::common::text;
    namespace env  = wxl::modern::assets::common::env;

    bool IsTextureComponent(std::string_view name)
    {
        return text::StartsWithCI(name, "item\\texturecomponents\\");
    }

    bool IsWorldTexture(std::string_view name)
    {
        return text::StartsWithCI(name, "world\\");
    }

    bool IsEnvironmentTexture(std::string_view name)
    {
        return IsWorldTexture(name)
            || text::StartsWithCI(name, "dungeon\\")
            || text::StartsWithCI(name, "dungeons\\")
            || text::StartsWithCI(name, "environment\\")
            || text::StartsWithCI(name, "tileset\\");
    }

    int FindConfigInt(const std::string& cfg, const char* key, int fallback)
    {
        const std::string needle = std::string("SET ") + key;
        size_t pos = cfg.find(needle);
        if (pos == std::string::npos) return fallback;
        pos += needle.size();
        while (pos < cfg.size() && std::isspace(static_cast<unsigned char>(cfg[pos]))) ++pos;
        if (pos < cfg.size() && cfg[pos] == '"') ++pos;
        bool neg = false;
        if (pos < cfg.size() && cfg[pos] == '-') { neg = true; ++pos; }
        int value = 0;
        bool any = false;
        while (pos < cfg.size() && std::isdigit(static_cast<unsigned char>(cfg[pos])))
        {
            any = true;
            value = value * 10 + int(cfg[pos] - '0');
            ++pos;
        }
        if (!any) return fallback;
        return neg ? -value : value;
    }

    double FindConfigNumber(const std::string& cfg, const char* key, double fallback)
    {
        const std::string needle = std::string("SET ") + key;
        size_t pos = cfg.find(needle);
        if (pos == std::string::npos) return fallback;
        pos += needle.size();
        while (pos < cfg.size() && std::isspace(static_cast<unsigned char>(cfg[pos]))) ++pos;
        if (pos < cfg.size() && cfg[pos] == '"') ++pos;
        char* end = nullptr;
        const char* begin = cfg.c_str() + pos;
        const double value = std::strtod(begin, &end);
        return end == begin ? fallback : value;
    }

    std::string ReadTextFile(const std::string& path)
    {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return {};
        std::string content;
        char buf[4096];
        for (;;)
        {
            const size_t n = std::fread(buf, 1, sizeof(buf), f);
            if (n) content.append(buf, n);
            if (n < sizeof(buf)) break;
        }
        std::fclose(f);
        return content;
    }

    const std::string& ClientConfigText()
    {
        static std::string cfg = []() {
            const std::string root = wxl::host::ClientRoot();
            return root.empty()
                ? ReadTextFile("WTF\\Config.wtf")
                : ReadTextFile(root + "\\WTF\\Config.wtf");
        }();
        return cfg;
    }

    uint32_t ConfiguredTextureEdge(const char* envKey, const char* configKey, uint32_t fallback)
    {
        const char* env = std::getenv(envKey);
        if (env && *env)
        {
            const unsigned long edge = std::strtoul(env, nullptr, 10);
            if (edge >= 64 && edge <= 4096) return static_cast<uint32_t>(edge);
        }

        const int configured = FindConfigInt(ClientConfigText(), configKey, 0);
        if (configured >= 64 && configured <= 4096)
            return static_cast<uint32_t>(configured);

        return fallback;
    }

    uint32_t ComponentTextureMaxEdge()
    {
        // Function-local static initialization is synchronized by C++11, unlike the former manual
        // loaded flag which raced when the prefetch pool and serve thread transformed their first BLP.
        static const uint32_t maxEdge = []() {
            const int componentLevel = FindConfigInt(ClientConfigText(), "componentTextureLevel", 0);
            uint32_t edge = 256;
            if (componentLevel >= 9) edge = 1024;
            else if (componentLevel >= 8) edge = 512;
            WLOG_INFO("modern-blp: componentTextureLevel=%d componentMaxEdge=%u",
                                   componentLevel, edge);
            return edge;
        }();
        return maxEdge;
    }

    uint32_t EnvironmentTextureMaxEdge()
    {
        static uint32_t maxEdge = []() {
            // Follow WoW's environment-detail axis for Low/custom profiles. Medium through Ultra retain the
            // established 512 cap; explicit WXL environment/world overrides remain authoritative.
            const double detail = FindConfigNumber(ClientConfigText(), "environmentDetail", 1.0);
            const uint32_t presetEdge = detail < 0.75 ? 256u : 512u;
            const uint32_t edge = ConfiguredTextureEdge("WXL_ENV_TEXTURE_MAX_EDGE",
                                                       "environmentTextureMaxEdge",
                                                       ConfiguredTextureEdge("WXL_WORLD_TEXTURE_MAX_EDGE",
                                                                             "worldTextureMaxEdge", presetEdge));
            WLOG_INFO("modern-blp: environmentDetail=%.2f environmentTextureMaxEdge=%u",
                                   detail, edge);
            return edge;
        }();
        return maxEdge;
    }

    // Larger edge a served terrain/model texture is capped to. Oversized modern art (2048/4096) is the
    // main consumer of the client's tight 32-bit address space; serving its existing 1024 mip cuts that
    // ~4x with no decode and no client change.
    constexpr uint32_t kMaxTextureEdge = 1024;

    bool TransformBlp(std::string_view name, std::span<const uint8_t> raw, std::vector<uint8_t>& out)
    {
        if (!text::EndsWithCI(name, ".blp")) return false;

        const uint32_t componentMaxEdge = ComponentTextureMaxEdge();
        if (IsTextureComponent(name) && wxl::modern::assets::textures::blp::TextureComponentToPaletted(raw, out, componentMaxEdge))
        {
            if (env::VerboseAssetLogs())
                WLOG_INFO("modern-blp: %.*s DXT component->paletted maxEdge=%u (%u -> %u bytes)",
                    int(name.size()), name.data(), componentMaxEdge, uint32_t(raw.size()), uint32_t(out.size()));
            return true;
        }

        // Cap oversized textures first (drops the top mip level), then transcode if the encoding needs it.
        const uint32_t maxTextureEdge = IsEnvironmentTexture(name) ? EnvironmentTextureMaxEdge() : kMaxTextureEdge;
        std::vector<uint8_t> capped;
        std::span<const uint8_t> src = raw;
        // EnvironmentTextureMaxEdge is a client-memory policy, not a modern-format compatibility
        // policy. Stock BLP2 world textures can also be 1024/2048 and were previously bypassing the
        // cap solely because IsModernTexture returned false. Busy zones then retained the original
        // top mips until Texture.cpp could no longer allocate even a small DXT surface. Cap every BLP2
        // environment texture; CapBlpMips strictly declines BLP1 and malformed data.
        const bool capEligible = wxl::host::IsModernTexture(name) || IsEnvironmentTexture(name);
        const bool didCap = capEligible &&
            wxl::modern::assets::textures::blp::CapBlpMips(raw, capped, maxTextureEdge);
        if (didCap) src = capped;

        std::vector<uint8_t> transcoded;
        if (wxl::modern::assets::textures::blp::TranscodeBlp(src, transcoded))
        {
            out = std::move(transcoded);
            if (env::VerboseAssetLogs())
                WLOG_INFO("modern-blp: %.*s %sBGRA->DXT5 (%u -> %u bytes)",
                    int(name.size()), name.data(), didCap ? "capped+" : "",
                    uint32_t(raw.size()), uint32_t(out.size()));
            return true;
        }
        if (didCap)
        {
            out = std::move(capped);
            if (env::VerboseAssetLogs())
                WLOG_INFO("modern-blp: %.*s capped to %u (%u -> %u bytes)",
                    int(name.size()), name.data(), maxTextureEdge, uint32_t(raw.size()), uint32_t(out.size()));
            return true;
        }
        return false;
    }

    struct Registrar
    {
        Registrar()
        {
            // Install the host BLP transform only when the modern-BLP format is compiled in; disabled, the registrar is inert.
            if constexpr (wxl::features::kModernBlp)
                wxl::host::RegisterTransform("modern-blp", &TransformBlp);
        }
    } g_registrar;
}
