// PatchScript: base class a patcher script subclasses to declare static PE edits.
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

#include <span>

#include "patcher/PeImage.hpp"

/**
 * @brief Base class for a patcher script: a subclass self-registers via its file-scope constructor and
 *        the patcher runs every registered script against the PE.
 */
namespace wxl::patcher
{
    class PatchScript
    {
    public:
        virtual ~PatchScript() = default;

        /**
         * @brief Returns a short identifier for the patcher log.
         * @return Script name.
         */
        virtual const char* name() const = 0;

        /**
         * @brief Applies this script's edits to the PE.
         * @param pe  PE image to edit.
         * @return True on success; false aborts the whole patch run.
         */
        virtual bool Apply(PeImage& pe) const = 0;

    protected:
        /** @brief Registers this script with the global registry. */
        PatchScript();
    };

    namespace registry
    {
        /**
         * @brief Adds a script to the registry.
         * @param script  script to register.
         */
        void Add(PatchScript* script);

        /**
         * @brief Returns all registered scripts.
         * @return Span over the registered scripts.
         */
        std::span<PatchScript* const> Scripts();
    }
}
