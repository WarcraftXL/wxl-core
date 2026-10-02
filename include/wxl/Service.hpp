// Service: a typed, lazily resolved handle on a capability another extension published, and the
// typed counterpart for publishing one.
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

#include "wxl/Script.hpp"

namespace wxl
{
    /**
     * A capability another extension published, resolved on first use and then kept.
     *
     * @param T : the service's interface struct, from the header the publisher ships
     *
     * Load order is not something an extension can rely on, so a service is looked up when it is
     * first needed rather than at load:
     *
     *     static wxl::Service<WXL_FdidApi> g_fdid("wxl.fdid", WXL_FDID_API_VERSION);
     *
     *     if (g_fdid) g_fdid->ResolveTexture(path, out, cap);
     *
     * A lookup that finds nothing is retried on the next use, so a service published later than the
     * first attempt is still picked up.
     */
    template <class T>
    class Service
    {
    public:
        /**
         * Names the service to resolve. Nothing is looked up yet.
         *
         * @param string name : the agreed service name, e.g. "wxl.fdid"
         * @param uint32 version : the agreed service version; a lookup only matches it exactly
         */
        constexpr Service(const char* name, uint32_t version) : name_(name), version_(version) {}

        /**
         * The interface, resolving it on the first call.
         *
         * @return T* service : null when nobody published it under that name and version
         */
        const T* Get() const
        {
            if (!iface_) iface_ = ScriptMgr::GetInterface<const T>(name_, version_);
            return iface_;
        }

        /// True when the service is available.
        explicit operator bool() const { return Get() != nullptr; }

        /**
         * The interface, for calling straight through.
         *
         * @return T* service : null when nobody published it; test the handle first
         */
        const T* operator->() const { return Get(); }

        /// The name this handle resolves.
        const char* Name() const { return name_; }
        /// The version this handle requires.
        uint32_t Version() const { return version_; }

    private:
        const char*       name_;
        uint32_t          version_;
        mutable const T*  iface_ = nullptr;
    };

    /**
     * Offers a capability to the other extensions, under a name and version they agree on.
     *
     * @param string name : the agreed service name, e.g. "wxl.db2"
     * @param uint32 version : the agreed service version
     * @param T* iface : the interface struct, which must outlive every consumer
     * @return bool ok : false before ScriptMgr::Bind
     *
     * The core stores the three without interpreting them, so two extensions can agree on anything
     * the service table does not model.
     */
    template <class T>
    inline bool Publish(const char* name, uint32_t version, const T* iface)
    {
        const WXL_Api* api = ScriptMgr::Api();
        if (!api) return false;
        api->PublishInterface(name, version, const_cast<T*>(iface));
        return true;
    }
}
