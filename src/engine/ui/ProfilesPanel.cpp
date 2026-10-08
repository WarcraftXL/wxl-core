// The overlay's Profiles panel (F9): save, load and delete named profiles of every settings set.
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
//
// For demos: a profile is every registered set's values (engine/script/Profiles.hpp). The list is
// read from disk when the panel opens and after every change, not every frame.

#include "engine/hook/Registry.hpp"
#include "engine/script/Profiles.hpp"
#include "engine/ui/ImGuiHost.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
    namespace ui       = wxl::ui::c;
    namespace profiles = wxl::script::profiles;

    char                     g_name[64] = {};
    std::vector<std::string> g_list;
    bool                     g_stale = true;
    std::string              g_status;
    std::string              g_pendingDelete;

    void Refresh()
    {
        g_list  = profiles::List();
        g_stale = false;
    }

    void __cdecl Draw(void*)
    {
        if (g_stale) Refresh();
        char line[160];
        std::snprintf(line, sizeof line, "Current: %s", *profiles::Current() ? profiles::Current() : "(none)");
        ui::Text(line);
        ui::Separator();

        ui::InputText("Name", g_name, sizeof g_name);
        ui::SameLine();
        if (ui::Button("Save"))
        {
            if (!profiles::ValidName(g_name))
                g_status = "A name is letters, digits, spaces, '-', '_' or '.', 48 at most.";
            else
                g_status = profiles::Save(g_name) ? std::string("Saved \"") + g_name + "\"." : "Could not write the file.";
            g_stale = true;
        }
        ui::SameLine();
        if (ui::Button("Refresh")) g_stale = true;
        if (!g_status.empty()) ui::Text(g_status.c_str());
        ui::Separator();

        if (g_list.empty()) ui::Text("No profile saved yet (WTF\\WarcraftXL\\profiles).");
        for (const std::string& name : g_list)
        {
            std::snprintf(line, sizeof line, "Load##%s", name.c_str());
            if (ui::Button(line))
            {
                g_status = profiles::Load(name.c_str()) ? "Loaded \"" + name + "\"." : "Could not read \"" + name + "\".";
                std::snprintf(g_name, sizeof g_name, "%s", name.c_str());
            }
            ui::SameLine();
            const bool confirm = g_pendingDelete == name;
            std::snprintf(line, sizeof line, "%s##%s", confirm ? "Really delete?" : "Delete", name.c_str());
            if (ui::Button(line))
            {
                if (confirm)
                {
                    g_status = profiles::Delete(name.c_str()) ? "Deleted \"" + name + "\"." : "Could not delete \"" + name + "\".";
                    g_pendingDelete.clear();
                    g_stale = true;
                }
                else
                {
                    g_pendingDelete = name;
                }
            }
            ui::SameLine();
            ui::Text(name.c_str());
        }
        ui::Text("WXL_PROFILE=<name> in WarcraftXL.cfg loads one at start.");
    }

    bool InstallProfilesPanel()
    {
        wxl::ui::AddPanel("Profiles", &Draw, nullptr);
        return true;
    }
}

WXL_REGISTER_FEATURE("profiles-panel", true, InstallProfilesPanel)
