// Wires the DB2 path tables into the host as a FileDataID resolver for the asset transforms.
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

#include "common/Log.hpp"
#include "mpq/MpqStore.hpp"

#include "db2/Db2Store.hpp"

#include <cstdint>
#include <mutex>
#include <string>

// The host owns the DB2 path tables (TextureFilePath / ModelFilePath) and answers FileDataID -> path. The
// modern ADT/WMO transforms reference textures and map objects by FileDataID; without resolution they fall
// back to placeholders ("missing.wmo"), which the Client opens and fatals on. This registers a resolver so
// those references map to real archive paths. The tables load once, lazily, on the first resolve (the
// client data root is not known at static-init time).
namespace wxl::scripts::hostext
{
    namespace
    {
        wxl::host::mpq::MpqStore g_store;
        wxl::host::db2::Db2Store g_db2;
        std::once_flag           g_once;
        bool                     g_ready = false;

        // Mount the archive set and decode the path tables. Runs exactly once.
        void LoadTables()
        {
            const std::string root = wxl::host::ClientRoot();
            if (root.empty() || !g_store.Mount(root))
            {
                WLOG_INFO("db2-resolver: archive mount FAILED (root '%s')", root.c_str());
                return;
            }
            g_ready = g_db2.Load(g_store);
            WLOG_INFO("db2-resolver: %s",
                g_ready ? "path tables loaded" : "no resolution tables (FDID resolution disabled)");
        }

        // Resolve a FileDataID to an archive path. Cold: called once per unresolved reference in a transform.
        bool Resolve(uint32_t fileDataId, std::string& outPath)
        {
            std::call_once(g_once, LoadTables);
            return g_ready && g_db2.ResolveFile(fileDataId, outPath);
        }

        // File-scope registrar: self-registers the resolver before the host serve loop starts.
        struct Registrar
        {
            Registrar() { wxl::host::RegisterResolver("db2", &Resolve); }
        } g_registrar;
    }
}
