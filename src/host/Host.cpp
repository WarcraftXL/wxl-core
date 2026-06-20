// Host hook surface: registers hooks and runs them from the serve pipeline.
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
        /** @brief Pairs a hook callback with its display name. */
        template <class Fn> struct Hook { const char* name; Fn fn; };

        // Function-local statics so a module file-scope registrar can register before main runs without a
        // static-init-order race against these lists.

        /** @brief Returns the transform hook list. */
        std::vector<Hook<TransformFn>>& Transforms() { static std::vector<Hook<TransformFn>> v; return v; }
        /** @brief Returns the provider hook list. */
        std::vector<Hook<ProvideFn>>&   Providers()  { static std::vector<Hook<ProvideFn>>   v; return v; }
        /** @brief Returns the exists hook list. */
        std::vector<Hook<ExistsFn>>&    Existers()   { static std::vector<Hook<ExistsFn>>    v; return v; }
        /** @brief Returns the served observer hook list. */
        std::vector<Hook<ServedFn>>&    Serveds()    { static std::vector<Hook<ServedFn>>    v; return v; }
    }

    /**
     * @brief Appends a transform hook to the list, ignoring a null callback.
     * @param name  display name for the hook
     * @param fn    transform callback
     */
    void RegisterTransform(const char* name, TransformFn fn)
    {
        if (!fn) return;
        Transforms().push_back({ name, fn });
        wxl::core::log::Printf("host: + transform hook '%s'", name ? name : "(unnamed)");
    }

    /**
     * @brief Appends a provider hook to the list, ignoring a null callback.
     * @param name  display name for the hook
     * @param fn    provider callback
     */
    void RegisterProvider(const char* name, ProvideFn fn)
    {
        if (!fn) return;
        Providers().push_back({ name, fn });
        wxl::core::log::Printf("host: + provider hook '%s'", name ? name : "(unnamed)");
    }

    /**
     * @brief Appends an exists hook to the list, ignoring a null callback.
     * @param name  display name for the hook
     * @param fn    exists callback
     */
    void RegisterExists(const char* name, ExistsFn fn)
    {
        if (!fn) return;
        Existers().push_back({ name, fn });
        wxl::core::log::Printf("host: + exists hook '%s'", name ? name : "(unnamed)");
    }

    /**
     * @brief Appends a served observer hook to the list, ignoring a null callback.
     * @param name  display name for the hook
     * @param fn    served callback
     */
    void RegisterServed(const char* name, ServedFn fn)
    {
        if (!fn) return;
        Serveds().push_back({ name, fn });
        wxl::core::log::Printf("host: + served hook '%s'", name ? name : "(unnamed)");
    }

    /**
     * @brief Runs each provider hook in order until one supplies the bytes.
     * @param name  file name requested
     * @param out   receives the supplied bytes
     * @return true if a provider claimed the file
     */
    bool Provide(std::string_view name, std::vector<uint8_t>& out)
    {
        for (const auto& h : Providers())
            if (h.fn(name, out)) return true;
        return false;
    }

    /**
     * @brief Runs each transform hook in order until one reshapes the raw bytes.
     * @param name  file name being served
     * @param raw   raw archive bytes
     * @param out   receives the reshaped bytes
     * @return true if a transform claimed the file
     */
    bool Transform(std::string_view name, std::span<const uint8_t> raw, std::vector<uint8_t>& out)
    {
        for (const auto& h : Transforms())
            if (h.fn(name, raw, out)) return true;
        return false;
    }

    /**
     * @brief Runs each exists hook in order until one reports the file present.
     * @param name  file name queried
     * @return true if any hook reports the file present
     */
    bool Exists(std::string_view name)
    {
        for (const auto& h : Existers())
            if (h.fn(name)) return true;
        return false;
    }

    /**
     * @brief Fires every served observer hook with the bytes handed to the client.
     * @param name   file name served
     * @param bytes  the bytes handed to the client
     */
    void NotifyServed(std::string_view name, std::span<const uint8_t> bytes)
    {
        for (const auto& h : Serveds())
            h.fn(name, bytes);
    }

    /** @brief Returns the storage holding the client data root. */
    std::string& ClientRootRef() { static std::string s; return s; }
    /**
     * @brief Stores the client data root.
     * @param root  client data root path
     */
    void SetClientRoot(std::string_view root) { ClientRootRef().assign(root); }
    /** @brief Returns the client data root. */
    std::string ClientRoot() { return ClientRootRef(); }

    /**
     * @brief Returns the total number of registered hooks across all hook points.
     * @return registered hook count
     */
    uint32_t HandlerCount()
    {
        return static_cast<uint32_t>(Transforms().size() + Providers().size() + Existers().size() + Serveds().size());
    }

    /** @brief Logs the registered hooks to the host log, grouped by hook point. */
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
