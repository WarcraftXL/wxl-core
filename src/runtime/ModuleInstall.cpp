// Module installer registry: runtime scripts self-register detour installers run from the main thread.
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

#include "runtime/ModuleInstall.hpp"

#include "core/Logger.hpp"

#include <windows.h>

#include <string>
#include <vector>

namespace wxl::runtime::modules
{
    namespace
    {
        struct Entry
        {
            const char* name;
            InstallFn   fn;
        };

        // Function-local statics so registration from global constructors never races init order.
        std::vector<Entry>& Registry()
        {
            static std::vector<Entry> v;
            return v;
        }

        std::vector<Entry>& BootRegistry()
        {
            static std::vector<Entry> v;
            return v;
        }

        std::vector<Entry>& EarlyRegistry()
        {
            static std::vector<Entry> v;
            return v;
        }

        bool IsDisabled(const char* name)
        {
            if (!name || !*name) return false;
            std::string flag = "WarcraftXL_";
            flag += name;
            flag += ".disable";
            return GetFileAttributesA(flag.c_str()) != INVALID_FILE_ATTRIBUTES;
        }
    }

    void Register(const char* name, InstallFn fn)
    {
        if (fn) Registry().push_back(Entry{ name ? name : "?", fn });
    }

    void RunAll()
    {
        for (const Entry& e : Registry())
        {
            if (IsDisabled(e.name))
            {
                WLOG_WARN("modules: disabled %s by client flag", e.name);
                continue;
            }
            e.fn();
            WLOG_INFO("modules: installed %s", e.name);
        }
    }

    void RegisterEarly(const char* name, InstallFn fn)
    {
        if (fn) EarlyRegistry().push_back(Entry{ name ? name : "?", fn });
    }

    void RunEarly()
    {
        for (const Entry& e : EarlyRegistry())
        {
            if (IsDisabled(e.name)) continue;
            e.fn();
            WLOG_INFO("modules: early-installed %s", e.name);
        }
    }

    void RegisterBoot(const char* name, InstallFn fn)
    {
        if (fn) BootRegistry().push_back(Entry{ name ? name : "?", fn });
    }

    void RunBoot()
    {
        for (const Entry& e : BootRegistry())
        {
            if (IsDisabled(e.name))
            {
                WLOG_WARN("modules: boot-disabled %s by client flag", e.name);
                continue;
            }
            e.fn();
            WLOG_INFO("modules: boot-installed %s", e.name);
        }
    }
}
