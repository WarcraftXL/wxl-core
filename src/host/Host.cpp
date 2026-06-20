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

#include "Host.hpp"

#include "core/Logger.hpp"

#include <string>
#include <vector>

namespace wxl::host
{
    namespace
    {
        template <class Fn> struct Hook { const char* name; Fn fn; };

        // Function-local statics so a module's file-scope registrar can register before main runs without a
        // static-init-order race against these lists.
        std::vector<Hook<TransformFn>>& Transforms() { static std::vector<Hook<TransformFn>> v; return v; }
        std::vector<Hook<ProvideFn>>&   Providers()  { static std::vector<Hook<ProvideFn>>   v; return v; }
        std::vector<Hook<ExistsFn>>&    Existers()   { static std::vector<Hook<ExistsFn>>    v; return v; }
        std::vector<Hook<ServedFn>>&    Serveds()    { static std::vector<Hook<ServedFn>>    v; return v; }
    }

    void RegisterTransform(const char* name, TransformFn fn)
    {
        if (!fn) return;
        Transforms().push_back({ name, fn });
        wxl::core::log::Printf("host: + transform hook '%s'", name ? name : "(unnamed)");
    }

    void RegisterProvider(const char* name, ProvideFn fn)
    {
        if (!fn) return;
        Providers().push_back({ name, fn });
        wxl::core::log::Printf("host: + provider hook '%s'", name ? name : "(unnamed)");
    }

    void RegisterExists(const char* name, ExistsFn fn)
    {
        if (!fn) return;
        Existers().push_back({ name, fn });
        wxl::core::log::Printf("host: + exists hook '%s'", name ? name : "(unnamed)");
    }

    void RegisterServed(const char* name, ServedFn fn)
    {
        if (!fn) return;
        Serveds().push_back({ name, fn });
        wxl::core::log::Printf("host: + served hook '%s'", name ? name : "(unnamed)");
    }

    bool Provide(std::string_view name, std::vector<uint8_t>& out)
    {
        for (const auto& h : Providers())
            if (h.fn(name, out)) return true;
        return false;
    }

    bool Transform(std::string_view name, std::span<const uint8_t> raw, std::vector<uint8_t>& out)
    {
        for (const auto& h : Transforms())
            if (h.fn(name, raw, out)) return true;
        return false;
    }

    bool Exists(std::string_view name)
    {
        for (const auto& h : Existers())
            if (h.fn(name)) return true;
        return false;
    }

    void NotifyServed(std::string_view name, std::span<const uint8_t> bytes, ServedOrigin origin)
    {
        for (const auto& h : Serveds())
            h.fn(name, bytes, origin);
    }

    std::string& ClientRootRef() { static std::string s; return s; }
    void SetClientRoot(std::string_view root) { ClientRootRef().assign(root); }
    std::string ClientRoot() { return ClientRootRef(); }

    uint32_t HandlerCount()
    {
        return static_cast<uint32_t>(Transforms().size() + Providers().size() + Existers().size() + Serveds().size());
    }

    void LogRegisteredHandlers()
    {
        wxl::core::log::Printf("host: %u hook(s) registered (transform=%zu provider=%zu exists=%zu served=%zu)",
            HandlerCount(), Transforms().size(), Providers().size(), Existers().size(), Serveds().size());
        for (const auto& h : Transforms()) wxl::core::log::Printf("host:   transform <- %s", h.name ? h.name : "(unnamed)");
        for (const auto& h : Providers())  wxl::core::log::Printf("host:   provider  <- %s", h.name ? h.name : "(unnamed)");
        for (const auto& h : Existers())   wxl::core::log::Printf("host:   exists    <- %s", h.name ? h.name : "(unnamed)");
        for (const auto& h : Serveds())    wxl::core::log::Printf("host:   served    <- %s", h.name ? h.name : "(unnamed)");
    }
}
