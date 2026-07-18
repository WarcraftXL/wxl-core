// Background pre-warming: resolver tables and neighbor map-tile transforms, off the client request thread.
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

#include "Warm.hpp"

#include "Host.hpp"
#include "Produce.hpp"
#include "Profile.hpp"

#include <windows.h>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace hprof = wxl::host::profile;

namespace wxl::host::warm
{
    namespace
    {
        // g_warmMutex protects g_warmQueue and g_warmSeen; g_warmCv is paired with g_warmMutex and signalled
        // whenever a new tile is queued.
        std::mutex                      g_warmMutex;
        std::condition_variable         g_warmCv;
        std::deque<std::string>         g_warmQueue;
        std::unordered_set<std::string> g_warmSeen;

        /**
         * @brief Splits a normalized "<prefix>_<x>_<y>.adt" tile key into its parts.
         * @return false when the key is not a tile name.
         */
        bool ParseTileName(const std::string& key, std::string& prefixOut, int& xOut, int& yOut)
        {
            if (key.size() < 9 || key.compare(key.size() - 4, 4, ".adt") != 0) return false;
            const size_t stemEnd = key.size() - 4;
            const size_t yUnd = key.rfind('_', stemEnd - 1);
            if (yUnd == std::string::npos || yUnd + 1 >= stemEnd) return false;
            const size_t xUnd = yUnd ? key.rfind('_', yUnd - 1) : std::string::npos;
            if (xUnd == std::string::npos || xUnd + 1 >= yUnd) return false;
            int x = 0, y = 0;
            for (size_t i = xUnd + 1; i < yUnd; ++i)
            {
                if (!std::isdigit(static_cast<unsigned char>(key[i]))) return false;
                x = x * 10 + (key[i] - '0');
            }
            for (size_t i = yUnd + 1; i < stemEnd; ++i)
            {
                if (!std::isdigit(static_cast<unsigned char>(key[i]))) return false;
                y = y * 10 + (key[i] - '0');
            }
            prefixOut = key.substr(0, xUnd);
            xOut = x;
            yOut = y;
            return true;
        }

        /** @brief Below-normal-priority worker producing queued neighbor tiles into the transform cache. */
        DWORD WINAPI TileWarmer(LPVOID)
        {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            for (;;)
            {
                std::string name;
                {
                    std::unique_lock<std::mutex> lk(g_warmMutex);
                    g_warmCv.wait(lk, [] { return !g_warmQueue.empty(); });
                    name = std::move(g_warmQueue.front());
                    g_warmQueue.pop_front();
                }
                hprof::OpenTrace trace; // warm-path stats stay out of the live request profile
                std::vector<uint8_t> bytes;
                bool nativeHit = false;
                wxl::host::produce::ProduceCandidate(name, name, bytes, trace, nativeHit);
            }
            return 0;
        }

        /** @brief Warms lazy FileDataID resolver tables below normal priority, off the client request thread. */
        DWORD WINAPI ResolverWarmer(LPVOID)
        {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            wxl::host::WarmResolvers();
            return 0;
        }
    }

    void StartResolverWarmer()
    {
        // Resolver tables are intentionally lazy because the client root is unknown during static init.
        // Give them a background head start now; ordinary archive requests remain responsive, and by the
        // time the first HD model needs an FDID the several-second cold table load is normally complete.
        if (HANDLE warmer = CreateThread(nullptr, 0, ResolverWarmer, nullptr, 0, nullptr))
            CloseHandle(warmer);
    }

    void StartTileWarmer()
    {
        // Pre-produces the neighbors of each served map tile so flight streaming hits a warm transform
        // cache instead of paying a cold tile transform inside a live open.
        if (HANDLE tileWarmer = CreateThread(nullptr, 0, TileWarmer, nullptr, 0, nullptr))
            CloseHandle(tileWarmer);
    }

    void QueueNeighborTiles(const std::string& servedName)
    {
        std::string prefix;
        int x = 0, y = 0;
        if (!ParseTileName(wxl::host::produce::NameKey(servedName), prefix, x, y)) return;
        bool queued = false;
        {
            std::lock_guard<std::mutex> lk(g_warmMutex);
            if (g_warmSeen.size() > 8192) g_warmSeen.clear(); // bounded memory; a re-warm is harmless
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if (!dx && !dy) continue;
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx > 63 || ny < 0 || ny > 63) continue;
                    if (g_warmQueue.size() >= 64) continue; // a burst is already pending
                    std::string n = prefix + '_' + std::to_string(nx) + '_' + std::to_string(ny) + ".adt";
                    if (g_warmSeen.insert(n).second)
                    {
                        g_warmQueue.push_back(std::move(n));
                        queued = true;
                    }
                }
        }
        if (queued) g_warmCv.notify_one();
    }
}
