// Overlay panel registry: a universal seam letting any module contribute a dev-overlay panel.
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

#include <functional>
#include <vector>

/**
 * @brief A registry of dev-overlay panels, decoupling the overlay host from the modules it shows.
 *
 * A module registers a draw callback (issuing ImGui::* calls) under a name; the overlay host
 * (wxl-devtools) invokes each registered panel inside its own ImGui frame. The host knows nothing
 * about the modules, and the panel content stays with the feature that owns it.
 */
namespace wxl::overlay
{
    /** @brief A panel's draw callback; called within the host's active ImGui frame. */
    using PanelFn = std::function<void()>;

    /** @brief A registered panel: a display name and its draw callback. */
    struct Panel { const char* name; PanelFn draw; };

    /**
     * @brief Registers a panel drawn by the overlay host each frame.
     * @param name  panel label (used as the section header).
     * @param draw  callback issuing ImGui::* calls for the panel body.
     */
    void RegisterPanel(const char* name, PanelFn draw);

    /** @brief The registered panels, in registration order. */
    const std::vector<Panel>& Panels();
}
