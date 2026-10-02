// The hook-point registry: the table in HookPoints.def, resolved by name for the core and for the
// extensions (WXL_Api::HookAttachByName).
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

#include "runtime/HookPoints.hpp"

#include "common/Log.hpp"
#include "engine/hook/Hook.hpp"

#include "wxl/offsets/engine/Addon.hpp"
#include "wxl/offsets/engine/Boot.hpp"
#include "wxl/offsets/engine/Camera.hpp"
#include "wxl/offsets/engine/Frame.hpp"
#include "wxl/offsets/engine/Gx.hpp"
#include "wxl/offsets/engine/GxDevice.hpp"
#include "wxl/offsets/engine/Io.hpp"
#include "wxl/offsets/engine/Liquid.hpp"
#include "wxl/offsets/engine/Lua.hpp"
#include "wxl/offsets/engine/Mem.hpp"
#include "wxl/offsets/engine/Shader.hpp"
#include "wxl/offsets/engine/Sky.hpp"
#include "wxl/offsets/engine/Sound.hpp"
#include "wxl/offsets/game/ADT.hpp"
#include "wxl/offsets/game/DB2.hpp"
#include "wxl/offsets/game/Doodad.hpp"
#include "wxl/offsets/game/GroundEffect.hpp"
#include "wxl/offsets/game/M2.hpp"
#include "wxl/offsets/game/Unit.hpp"
#include "wxl/offsets/game/Weather.hpp"
#include "wxl/offsets/game/WMO.hpp"
#include "wxl/offsets/game/World.hpp"
#include "wxl/offsets/game/WorldMap.hpp"
#include "wxl/offsets/game/WorldScene.hpp"

#include <cstring>

namespace wxl::runtime::hookpoints
{
    namespace
    {
        namespace adt    = wxl::offsets::game::adt;
        namespace boot   = wxl::offsets::engine::boot;
        namespace cam    = wxl::offsets::engine::camera;
        namespace addon  = wxl::offsets::engine::addon;
        namespace db2    = wxl::offsets::game::db2;
        namespace dd     = wxl::offsets::game::doodad;
        namespace frm    = wxl::offsets::engine::frame;
        namespace grass  = wxl::offsets::game::groundeffect;
        namespace gxoff  = wxl::offsets::engine::gx;
        namespace devoff = wxl::offsets::engine::gxdevice;
        namespace io     = wxl::offsets::engine::io;
        namespace liq    = wxl::offsets::engine::liquid;
        namespace lua    = wxl::offsets::engine::lua;
        namespace mem    = wxl::offsets::engine::mem;
        namespace m2     = wxl::offsets::game::m2;
        namespace shoff  = wxl::offsets::engine::shader;
        namespace sky    = wxl::offsets::engine::sky;
        namespace snd    = wxl::offsets::engine::sound;
        namespace unit   = wxl::offsets::game::unit;
        namespace wld    = wxl::offsets::game::world;
        namespace wmo    = wxl::offsets::game::wmo;
        namespace wthr   = wxl::offsets::game::weather;
        namespace wmap   = wxl::offsets::game::worldmap;
        namespace wscene = wxl::offsets::game::worldscene;

        struct Point
        {
            const char* name;
            uintptr_t   address;
        };

        constexpr Point kPoints[] = {
#define WXL_HOOK_POINT(name, address) { name, address },
#include "runtime/HookPoints.def"
#undef WXL_HOOK_POINT
        };

        const Point* Find(const char* name)
        {
            if (!name) return nullptr;
            for (const Point& p : kPoints)
                if (std::strcmp(p.name, name) == 0) return &p;
            return nullptr;
        }
    }

    int AttachByName(const char* pointName, void* detour, void** original, int priority)
    {
        const Point* p = Find(pointName);
        if (!p)
        {
            WLOG_ERROR("hookpoints: '%s' is not a registered hook point", pointName ? pointName : "(null)");
            return 0;
        }
        return wxl::hook::Install(pointName, p->address, detour, original, priority) ? 1 : 0;
    }

    size_t Count() { return sizeof kPoints / sizeof kPoints[0]; }
}
