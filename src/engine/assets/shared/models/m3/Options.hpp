// Drop-in model source description and its optional sidecar options file.
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

#pragma once

#include <string>

namespace wxl::modern::assets::m3
{
    /**
     * @brief One drop-in model: a source .m3 living at the served path, plus options.
     *
     * "<dir>\<base>.m3" in a loose data folder serves as "<dir>\<base>.m2"; the optional
     * "<base>.m3a" library and "<base>.m3cfg" options sit beside it. Sidecar keys:
     *   map  = Stand:0,Walk A:4@1@2.6,...   ; sequence mapping, built-in default otherwise
     *   tex  = @skin:creature\x\            ; texture mode override (display-row variations)
     *   lift = 0.05                         ; z lift
     *   parcolor = 60,255,90                ; particle tint 0-255 (source tints live in
     *                                       ; game data, not the model: white otherwise)
     *   ribmesh = 1                         ; ribbons are permanent sheets (wings, banners):
     *                                       ; bake them as strip meshes instead of trails
     */
    struct ModelSource
    {
        std::string target;   // served path, lowercase backslash form, ".m2" included
        std::string m3;       // source model file
        std::string m3a;      // animation library file, empty = static model
        std::string map;      // sequence mapping, empty = default
        std::string tex;      // texture prefix or "@skin:<prefix>"
        std::string texroot;  // folder of source .dds textures
        float       lift = 0.0f;
        float       parcolor[3] = { 255.0f, 255.0f, 255.0f };
        bool        ribmesh = false;
        float       ribtilt = 30.0f; // sheet angle: 0 = bone local Z, 90 = local X
        float       riblen = 1.0f;   // sheet length scale, texel density preserved
        float       ribbasewidth = 1.0f; // base width / tip width ratio; tip stays full width
        std::string ambient;  // always-on sequence from the model file itself (e.g. GLstand)
    };

    /**
     * @brief Applies sidecar key=value option lines onto a source description.
     * @param text  whole sidecar file content
     * @param src   source to fill (m3a / map / tex / texroot / lift keys)
     */
    void ParseOptions(const std::string& text, ModelSource& src);

    /** @brief Lowercases and normalizes separators of a served path. */
    std::string NormalizePath(std::string s);
}
