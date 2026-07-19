// FileDataID resolver -- implementation. Lazy-loads the custom DB2s through the client IO layer.
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

#include "features/fdid/Fdid.hpp"

#include "engine/db2/Db2Table.hpp"
#include "engine/db2/schema/TextureFilePath.hpp"
#include "engine/db2/schema/ModelFilePath.hpp"

#include "common/Log.hpp"
#include "offsets/engine/Io.hpp"

#include <mutex>
#include <vector>

namespace wxl::fdid
{
    namespace
    {
        namespace io = wxl::offsets::engine::io;

        // One lazily-loaded typed table plus its "already tried" latch, so a missing/broken DB2 is
        // probed once and then treated as permanently empty (no per-lookup reopen storms).
        template <class Schema>
        struct LazyTable
        {
            wxl::db2::Db2Table<Schema> table;
            bool                       tried = false;
            std::once_flag             once;
        };

        LazyTable<wxl::db2::schema::TextureFilePath> g_tex;
        LazyTable<wxl::db2::schema::ModelFilePath>   g_model;

        // Read a whole archive/loose file into memory via the client storage primitives (same
        // by-address calls AdtSplit uses, so any host-file redirect detours still apply).
        bool ReadWholeFile(const char* name, std::vector<uint8_t>& out)
        {
            void* h = nullptr;
            if (!reinterpret_cast<io::Storage_FileOpenFn>(io::kFileOpen)(nullptr, name, 0, &h) || !h)
                return false;
            const uint32_t size = reinterpret_cast<io::Storage_FileSizeFn>(io::kFileSize)(h, nullptr);
            bool ok = false;
            if (size != 0 && size != 0xFFFFFFFFu)
            {
                out.resize(size);
                ok = reinterpret_cast<io::Storage_FileReadFn>(io::kFileRead)(h, out.data(), size, nullptr, nullptr, 0) != 0;
            }
            reinterpret_cast<io::Storage_FileCloseFn>(io::kFileClose)(h);
            return ok;
        }

        template <class Schema>
        void EnsureLoaded(LazyTable<Schema>& lt)
        {
            std::call_once(lt.once, [&] {
                lt.tried = true;
                std::vector<uint8_t> bytes;
                if (!ReadWholeFile(Schema::kFile, bytes))
                {
                    WLOG_WARN("[fdid] %s not found -- FileDataID resolution disabled for it", Schema::kFile);
                    return;
                }
                if (lt.table.Load(std::move(bytes)))
                    WLOG_INFO("[fdid] %s loaded (%u records)", Schema::kFile, lt.table.RecordCount());
                else
                    WLOG_ERROR("[fdid] %s failed to parse", Schema::kFile);
            });
        }
    } // namespace

    const char* ResolveTexture(uint32_t fileDataId)
    {
        EnsureLoaded(g_tex);
        if (!g_tex.table.Ok() || fileDataId == 0)
            return nullptr;
        wxl::db2::schema::TextureFilePath rec;
        if (!g_tex.table.Find(fileDataId, rec) || !rec.filePath[0])
            return nullptr;
        return rec.filePath;
    }

    const char* ResolveModel(uint32_t fileDataId)
    {
        EnsureLoaded(g_model);
        if (!g_model.table.Ok() || fileDataId == 0)
            return nullptr;
        wxl::db2::schema::ModelFilePath rec;
        if (!g_model.table.Find(fileDataId, rec) || !rec.filePath[0])
            return nullptr;
        return rec.filePath;
    }

    bool     TextureTableReady()   { EnsureLoaded(g_tex);   return g_tex.table.Ok(); }
    bool     ModelTableReady()     { EnsureLoaded(g_model); return g_model.table.Ok(); }
    uint32_t TextureRecordCount()  { EnsureLoaded(g_tex);   return g_tex.table.RecordCount(); }
    uint32_t ModelRecordCount()    { EnsureLoaded(g_model); return g_model.table.RecordCount(); }
}
