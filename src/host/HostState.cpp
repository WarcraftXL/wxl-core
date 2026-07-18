// Host process state: the client data root and the modern-sourced texture provenance set.
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

#include "Host.hpp"

#include <cctype>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

namespace wxl::host
{
    /** @brief Returns the storage holding the client data root. */
    std::string& ClientRootRef() { static std::string s; return s; }
    /**
     * @brief Stores the client data root.
     * @param root  client data root path
     */
    void SetClientRoot(std::string_view root) { ClientRootRef().assign(root); }
    /** @brief Returns the client data root. */
    std::string ClientRoot() { return ClientRootRef(); }

    namespace
    {
        /** @brief Lowercases and swaps '/' for '\\' so two spellings of the same path compare equal. */
        std::string NormalizeTexturePath(std::string_view path)
        {
            std::string s(path);
            for (char& c : s)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (c == '/') c = '\\';
            }
            return s;
        }

        /** @brief Guards ModernTextureSet(). */
        std::mutex& ModernTextureMutex() { static std::mutex m; return m; }
        /** @brief Returns the process-wide set of modern-sourced texture paths. */
        std::unordered_set<std::string>& ModernTextureSet() { static std::unordered_set<std::string> s; return s; }
    }

    void MarkModernTexture(std::string_view path)
    {
        std::lock_guard<std::mutex> lock(ModernTextureMutex());
        ModernTextureSet().insert(NormalizeTexturePath(path));
    }

    bool IsModernTexture(std::string_view path)
    {
        std::lock_guard<std::mutex> lock(ModernTextureMutex());
        const auto& set = ModernTextureSet();
        return set.find(NormalizeTexturePath(path)) != set.end();
    }
}
