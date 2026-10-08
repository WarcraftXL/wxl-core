// Named profiles over every registered settings set: WTF\WarcraftXL\profiles\<name>.cfg.
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
#include <vector>

namespace wxl::script::profiles
{
    /// A name a profile may take: 1..48 of letters, digits, space, '-', '_', '.' (no leading '.').
    bool ValidName(const char* name);

    /**
     * Writes every registered set's keys to the profile, one [set] section each.
     *
     * @return bool ok : false for a bad name or a file that could not be written
     */
    bool Save(const char* name);

    /**
     * Applies a profile through each set's Set, in the file's order; unknown sets and keys, and the
     * keys a set refuses (read-only ones), are skipped.
     *
     * @return bool ok : false for a bad name or a missing file
     */
    bool Load(const char* name);

    /// Deletes a profile's file. False for a bad name or a missing file.
    bool Delete(const char* name);

    /// The profiles on disk, sorted by name.
    std::vector<std::string> List();

    /// The profile last loaded or saved this session, "" for none.
    const char* Current();

    /// Loads WXL_PROFILE (environment, then WarcraftXL.cfg) once; every set registers before the
    /// first interface load, which is where this is called.
    void LoadStartupOnce();
}
