// wxl-glue-unlock (patcher script): bypass the client's Lua/XML signature check, so custom
// interface code loads. A PatchScript: it owns the byte set and calls the core PE toolbox.
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
// Byte set ported from a third-party extension pack (MIT, (c) Alyst3r).

#include "patcher/PatchScript.hpp"

namespace
{
    using namespace wxl::patcher;

    class GlueUnlock final : public PatchScript
    {
    public:
        const char* name() const override { return "glue-unlock"; }

        bool Apply(PeImage& pe) const override
        {
            struct Edit { uint32_t va; uint8_t bytes[8]; uint32_t len; };
            const Edit edits[] = {
                { 0x5F4DBF, { 0xEB },                               1 },
                { 0x816625, { 0xEB },                               1 },
                { 0x81663F, { 0x03 },                               1 },
                { 0x816695, { 0x03 },                               1 },
                { 0x816746, { 0xEB },                               1 },
                { 0x81675F, { 0xB8, 0x03, 0x00, 0x00, 0x00, 0xEB, 0xED }, 7 },
            };
            for (const Edit& e : edits)
                if (!pe.WriteVa(e.va, e.bytes, e.len)) return false;
            return true;
        }
    };

    const GlueUnlock g_glueUnlock;
}
