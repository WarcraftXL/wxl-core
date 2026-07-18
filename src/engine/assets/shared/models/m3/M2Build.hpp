// Client-model builder: bakes source animations and writes the model + skin images.
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

#include "M3Model.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wxl::modern::assets::m3
{
    /** @brief One source-sequence to client-animation mapping entry. */
    struct SeqMap
    {
        std::string name;      // source sequence name
        uint16_t    wowId;     // client animation id
        float       tscale;    // time scale (0.65 = plays ~1.5x faster)
        float       speed;     // movespeed override, <0 = table default
    };

    /**
     * @brief Parses "Name:id[@tscale[@speed]],..." into mapping entries.
     * @param mapping  the mapping string; empty selects the built-in default
     * @return parsed entries
     */
    std::vector<SeqMap> ParseSeqMap(const std::string& mapping);

    /** @brief One baked client sequence. */
    struct BakedSeq
    {
        uint16_t id;
        uint32_t duration;
        float    speed;    // <0 = table default at write time
    };

    /** @brief One bone's baked keys for one sequence. */
    struct BakedKeys
    {
        std::vector<uint32_t> times;
        std::vector<Vec3>     locs;
        std::vector<Quat>     rots;
        std::vector<Vec3>     scales;
    };

    /** @brief One emitter's baked scalar keys (e.g. particle rate) for one sequence. */
    struct BakedScalar
    {
        std::vector<uint32_t> times;
        std::vector<float>    values;
    };

    struct BakeResult
    {
        std::vector<BakedSeq> seqs;
        // [bone][seq]: nullopt = bone static in that sequence
        std::vector<std::vector<std::optional<BakedKeys>>> tracks;
        // [emitter][seq]: nullopt = no library track, caller falls back to the static rate
        std::vector<std::vector<std::optional<BakedScalar>>> emitterRate;
        std::vector<Vec3> pivots;
    };

    /**
     * @brief Bakes the mapped library sequences onto the model skeleton.
     *
     * Track values are absolute parent-local transforms; bones match BY NAME (the
     * library skeleton is a differently-ordered superset).
     * @param model  parsed model (frame-adjusted rest matrices)
     * @param lib    parsed animation library
     * @param map    sequence mapping
     * @param frame  model-frame matrix (the root bones' virtual parent)
     * @param out    receives the bake
     * @return true when at least one sequence baked
     */
    bool BakeSequences(const Model& model, const AnimLib& lib, const std::vector<SeqMap>& map,
                       const Mat4& frame, BakeResult& out);

    /**
     * @brief Produces a static single-sequence bake (no animation library).
     * @param model  parsed model
     * @param out    receives the bake
     */
    void StaticBake(const Model& model, BakeResult& out);

    /**
     * @brief Overlays one always-on source sequence onto every baked sequence.
     *
     * Bones the main bake left static get the named sequence's tracks in every
     * client sequence (ambient loops: wing waves, idle glows). Bones the main
     * library animates keep their tracks.
     * @param model    parsed model
     * @param self     the model file itself parsed as an animation library
     * @param seqName  source sequence name (e.g. "GLstand")
     * @param frame    model-frame matrix
     * @param out      bake to extend
     * @return true when at least one bone received tracks
     */
    bool OverlayAmbient(const Model& model, const AnimLib& self, const std::string& seqName,
                        const Mat4& frame, BakeResult& out);

    /**
     * @brief Writes the client model image.
     * @param model     parsed model
     * @param name      internal model name
     * @param texMode   texture prefix; "@skin:<prefix>" routes opaque diffuse materials
     *                  through the creature-skin slots (display-row texture variations)
     * @param bake      baked sequences and bone tracks
     * @param tint      particle color multiplier 0-255 per channel, null = white
     * @param out       receives the model bytes
     * @param fxTextures  when non-null, appended with every served texture path used as a
     *                    particle or ribbon atlas (additive-blend FX, as opposed to a material's
     *                    own diffuse skin) -- callers use this to route those specific textures
     *                    through luminance-derived alpha at serve time
     */
    void BuildM2(const Model& model, const std::string& name, const std::string& texMode,
                 const BakeResult& bake, const float* tint, std::vector<uint8_t>& out,
                 std::vector<std::string>* fxTextures = nullptr);

    /**
     * @brief Writes the skin (view 00) image for the model.
     * @param model  parsed model
     * @param out    receives the skin bytes
     */
    void BuildSkin(const Model& model, std::vector<uint8_t>& out);
}
