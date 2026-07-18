// Feature installer registry: features self-register before main, the bootstrap unrolls them.
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

#include "engine/hook/Registry.hpp"

#include "common/Log.hpp"

#include <vector>

namespace wxl::hook
{
    namespace
    {
        struct Entry
        {
            const char* name;
            bool enabled;
            bool (*install)();
            Phase phase;
        };

        /// Function-local static: constructed on first RegisterFeature call, so it is guaranteed
        /// alive when static registrars run before main regardless of link order.
        std::vector<Entry>& Registry()
        {
            static std::vector<Entry> entries;
            return entries;
        }
    }

    void RegisterFeature(const char* name, bool enabled, bool (*install)(), Phase phase)
    {
        Registry().push_back(Entry{name, enabled, install, phase});
    }

    int InstallRegisteredFeatures(Phase phase)
    {
        int installed = 0;
        for (const Entry& entry : Registry())
        {
            if (!entry.enabled || entry.phase != phase) continue;
            if (entry.install && entry.install())
            {
                WLOG_DEBUG("feature: installed '%s'", entry.name);
                ++installed;
            }
            else
            {
                WLOG_ERROR("feature: install '%s' failed", entry.name);
            }
        }
        return installed;
    }
}
