// WMO method context: the wxl.wmo subtable reading a map-object root/group event pointer's fields.
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

#include "engine/lua/LuaJit.hpp"
#include "engine/lua/Marshal.hpp"
#include "engine/lua/methods/PointerArg.hpp"
#include "features/wmonative/OutdoorGate.hpp"
#include "features/wmonative/WmoNative.hpp"
#include "game/wmo/Wmo.hpp"
#include "offsets/game/WMO.hpp"

#include <windows.h>

/// The WMO context, in the CameraMethods mould: Register() adds the wxl.wmo subtable to the `wxl` table
/// on the stack. The root fields take the pointer a "wmo_root_load" handler received as LIGHTUSERDATA;
/// the group field takes a "wmo_group_load" pointer. Each read is an SEH-guarded POD copy off a confirmed
/// offset, because the game accessors here only null-check (they do not range-guard an arbitrary event
/// pointer). interior_path() takes no argument: it reads the map-object the camera is currently inside.
/// Header-only and inline.
namespace wxl::lua::methods::wmo
{
    namespace gw  = wxl::game::wmo;
    namespace off = wxl::offsets::game::wmo;

    /// The root inline path buffer is bounded by the client; cap our copy conservatively.
    inline constexpr int kNameCap = 256;

    /// wxl.wmo.name(root) -> string or nil (inline map-object path at root+kOffNameInline). root is a
    /// wmo_root_load event lightuserdata.
    inline int L_name(lua_State* L)
    {
        void* root = CheckPtr(L, 1);
        char buf[kNameCap];
        if (!root || SehReadCStr(root, off::kOffNameInline, buf, kNameCap) == 0)
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<const char*>(buf));
        return 1;
    }

    /// wxl.wmo.root_size(root) -> int or nil (root buffer byte size). root is a wmo_root_load event
    /// lightuserdata.
    inline int L_rootSize(lua_State* L)
    {
        void* root = CheckPtr(L, 1);
        uint32_t v = 0;
        if (!root || !SehReadU32(root, off::kOffRootSize, v))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<lua_Integer>(v));
        return 1;
    }

    /// wxl.wmo.group_count(root) -> int or nil (number of groups). root is a wmo_root_load event
    /// lightuserdata.
    inline int L_groupCount(lua_State* L)
    {
        void* root = CheckPtr(L, 1);
        uint32_t v = 0;
        if (!root || !SehReadU32(root, off::kOffGroupCount, v))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<lua_Integer>(v));
        return 1;
    }

    /// wxl.wmo.material_count(root) -> int or nil (number of materials). root is a wmo_root_load event
    /// lightuserdata.
    inline int L_materialCount(lua_State* L)
    {
        void* root = CheckPtr(L, 1);
        uint32_t v = 0;
        if (!root || !SehReadU32(root, off::kOffMaterialCount, v))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<lua_Integer>(v));
        return 1;
    }

    /// wxl.wmo.group_size(group) -> int or nil (group buffer byte size). group is a wmo_group_load event
    /// lightuserdata.
    inline int L_groupSize(lua_State* L)
    {
        void* group = CheckPtr(L, 1);
        uint32_t v = 0;
        if (!group || !SehReadU32(group, off::kOffGroupSize, v))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<lua_Integer>(v));
        return 1;
    }

    /// wxl.wmo.interior_path() -> string or nil: the inline path of the map-object the camera is currently
    /// inside (nil outdoors). Reads a live engine global; no argument.
    inline int L_interiorPath(lua_State* L)
    {
        const char* p = nullptr;
        __try
        {
            p = gw::CurrentInteriorPath();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            p = nullptr;
        }
        char buf[kNameCap];
        if (!p || SehReadCStr(p, 0, buf, kNameCap) == 0)
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<const char*>(buf));
        return 1;
    }

    // --- native modern-WMO reader (features/wmonative) ---

    /// wxl.wmo.native_installed() -> bool: true when every walker detour is live.
    inline int L_nativeInstalled(lua_State* L)
    {
        Push(L, wxl::runtime::wmonative::Installed());
        return 1;
    }

    /// wxl.wmo.native_stats() -> table of the native reader's session counters.
    inline int L_nativeStats(lua_State* L)
    {
        const auto s = wxl::runtime::wmonative::GetStats();
        lua_newtable(L);
        Push(L, static_cast<lua_Integer>(s.rootsModern));        lua_setfield(L, -2, "roots_modern");
        Push(L, static_cast<lua_Integer>(s.rootsStock));          lua_setfield(L, -2, "roots_stock");
        Push(L, static_cast<lua_Integer>(s.groupsModern));        lua_setfield(L, -2, "groups_modern");
        Push(L, static_cast<lua_Integer>(s.groupsFailed));        lua_setfield(L, -2, "groups_failed");
        Push(L, static_cast<lua_Integer>(s.texturesResolved));    lua_setfield(L, -2, "textures_resolved");
        Push(L, static_cast<lua_Integer>(s.texturesUnresolved));  lua_setfield(L, -2, "textures_unresolved");
        Push(L, static_cast<lua_Integer>(s.parkedDoodadDefs));    lua_setfield(L, -2, "parked_doodad_defs");
        Push(L, static_cast<lua_Integer>(s.parkedLiquids));       lua_setfield(L, -2, "parked_liquids");
        Push(L, static_cast<lua_Integer>(s.parkedIndex32));       lua_setfield(L, -2, "parked_index32");
        Push(L, static_cast<lua_Integer>(s.parkedNoUvGroups));    lua_setfield(L, -2, "parked_no_uv_groups");
        Push(L, static_cast<lua_Integer>(s.unknownChunks));       lua_setfield(L, -2, "unknown_chunks");
        Push(L, static_cast<lua_Integer>(s.shaderRemapped));      lua_setfield(L, -2, "shader_remapped");
        Push(L, static_cast<lua_Integer>(s.shaderToTwoLayer));    lua_setfield(L, -2, "shader_to_two_layer");
        Push(L, static_cast<lua_Integer>(s.shaderToEnv));         lua_setfield(L, -2, "shader_to_env");
        Push(L, static_cast<lua_Integer>(s.shaderToSingle));      lua_setfield(L, -2, "shader_to_single");
        Push(L, static_cast<lua_Integer>(s.materialIdsMoved));    lua_setfield(L, -2, "material_ids_moved");
        Push(L, static_cast<lua_Integer>(s.materialIdsOutOfRange)); lua_setfield(L, -2, "material_ids_out_of_range");
        Push(L, static_cast<lua_Integer>(s.batchCullBypassed));   lua_setfield(L, -2, "batch_cull_bypassed");
        return 1;
    }

    // --- indoor/outdoor gate (features/wmonative/OutdoorGate) ---

    /// wxl.wmo.outdoor_stats() -> table: how often the added exterior rule actually fires.
    inline int L_outdoorStats(lua_State* L)
    {
        const auto s = wxl::runtime::wmooutdoor::GetStats();
        lua_newtable(L);
        Push(L, static_cast<lua_Integer>(s.framesIndoor));      lua_setfield(L, -2, "frames_indoor");
        Push(L, static_cast<lua_Integer>(s.framesReopened));    lua_setfield(L, -2, "frames_reopened");
        Push(L, static_cast<lua_Integer>(s.framesPortalOpen));  lua_setfield(L, -2, "frames_portal_open");
        Push(L, static_cast<lua_Integer>(s.groupResolveFails)); lua_setfield(L, -2, "group_resolve_fails");
        Push(L, wxl::runtime::wmooutdoor::Installed());         lua_setfield(L, -2, "installed");
        Push(L, wxl::runtime::wmooutdoor::RuleEnabled());       lua_setfield(L, -2, "rule_enabled");
        Push(L, static_cast<lua_Integer>(s.occludersSkipped));  lua_setfield(L, -2, "occluders_skipped");
        Push(L, static_cast<lua_Integer>(s.occludersBuilt));    lua_setfield(L, -2, "occluders_built");
        Push(L, wxl::runtime::wmooutdoor::OccludersSkipped());  lua_setfield(L, -2, "occluders_disabled");
        Push(L, static_cast<lua_Integer>(s.undersideEdges));    lua_setfield(L, -2, "underside_edges");
        Push(L, static_cast<lua_Integer>(s.boxesUnculled));     lua_setfield(L, -2, "boxes_unculled");
        Push(L, wxl::runtime::wmooutdoor::UndersideRuleEnabled()); lua_setfield(L, -2, "underside_rule");
        return 1;
    }

    /// wxl.wmo.set_occluder_underside_rule(bool): live A/B of the lower bound added to the occlusion
    /// skyline. False restores the stock skyline, so a bridge deck hides the ground under it again.
    inline int L_setUndersideRule(lua_State* L)
    {
        wxl::runtime::wmooutdoor::SetUndersideRuleEnabled(CheckBool(L, 1));
        return 0;
    }

    /// wxl.wmo.set_modern_occluders_disabled(bool): live A/B of the antiportal occluders a modern WMO
    /// contributes. True (default) builds none; false restores the stock angular occluders.
    inline int L_setModernOccludersDisabled(lua_State* L)
    {
        wxl::runtime::wmooutdoor::SetOccludersSkipped(CheckBool(L, 1));
        return 0;
    }

    /// wxl.wmo.set_outdoor_rule_enabled(bool): live A/B of Legion's exterior re-enable rule. False
    /// restores the stock "portal only" behaviour, so the terrain-blanking can be seen come back.
    inline int L_setOutdoorRuleEnabled(lua_State* L)
    {
        wxl::runtime::wmooutdoor::SetRuleEnabled(CheckBool(L, 1));
        return 0;
    }

    /**
     * @brief Adds the wxl.wmo subtable to the table on top of the stack. Stack-neutral.
     * @param L  the engine lua_State, with the target `wxl` table at index -1.
     */
    inline void Register(lua_State* L)
    {
        static const luaL_Reg fns[] = {
            { "name",           L_name },
            { "root_size",      L_rootSize },
            { "group_count",    L_groupCount },
            { "material_count", L_materialCount },
            { "group_size",     L_groupSize },
            { "interior_path",  L_interiorPath },
            { "native_installed", L_nativeInstalled },
            { "native_stats",     L_nativeStats },
            { "outdoor_stats",    L_outdoorStats },
            { "set_outdoor_rule_enabled", L_setOutdoorRuleEnabled },
            { "set_modern_occluders_disabled", L_setModernOccludersDisabled },
            { "set_occluder_underside_rule", L_setUndersideRule },
            { nullptr, nullptr },
        };
        RegisterModule(L, "wmo", fns);
    }
}
