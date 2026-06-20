// Host: the host hook surface. The host serve pipeline fires these; modules' host faces register into them.
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

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// THE host hook surface. WarcraftXLHost.exe (64-bit) owns the archive set + the IPC transport and knows
// NOTHING about asset formats. A module contributes a host face under scripts/<module>/host/ that, from a
// file-scope registrar, hooks the host serve pipeline here at startup. This header is the single place a
// module author consults to see where it can plug in.
//
// Every hook below is FIRED by the host serve pipeline on the matching request, even with no registrant
// (then it is a no-op and the host serves raw archive bytes) -- the same fire-to-zero-or-more model as the
// DLL event bus. Handlers run in registration order; the first that claims a request wins.
namespace wxl::host
{
    using TransformFn = bool (*)(std::string_view name, std::span<const uint8_t> raw, std::vector<uint8_t>& out);
    void RegisterTransform(const char* name, TransformFn fn);

    using ProvideFn = bool (*)(std::string_view name, std::vector<uint8_t>& out);
    void RegisterProvider(const char* name, ProvideFn fn);

    using ExistsFn = bool (*)(std::string_view name);
    void RegisterExists(const char* name, ExistsFn fn);

    enum class ServedOrigin { Client, Warm };
    using ServedFn = void (*)(std::string_view name, std::span<const uint8_t> bytes, ServedOrigin origin);
    void RegisterServed(const char* name, ServedFn fn);

    // --- pipeline entry points (called by the host serve loop, NOT by modules) ---

    bool Provide(std::string_view name, std::vector<uint8_t>& out);
    bool Transform(std::string_view name, std::span<const uint8_t> raw, std::vector<uint8_t>& out);
    bool Exists(std::string_view name);
    void NotifyServed(std::string_view name, std::span<const uint8_t> bytes, ServedOrigin origin);

    // --- host environment (set by the host at startup; read by modules that read the archives themselves,
    //     e.g. the prefetch pool mounting its own per-thread MpqStore) ---

    void SetClientRoot(std::string_view root);
    std::string ClientRoot();

    // --- registry introspection ---

    // Total registered hooks across all three points (how much the host was extended).
    uint32_t HandlerCount();
    // Log the registered hooks to the host log (the visible list of who extended the host, by hook point).
    void LogRegisteredHandlers();
}
