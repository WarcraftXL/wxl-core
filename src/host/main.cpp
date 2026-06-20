// WarcraftXLHost.exe (64-bit): archive owner + IPC transport. Format-blind: asset reshaping is the
// registered handlers' job (modules contribute them). Serves raw bytes for anything no handler claims.
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
#include "ipc/Protocol.hpp"
#include "ipc/ShmServer.hpp"
#include "mpq/MpqStore.hpp"
#include "core/Logger.hpp"

#include <flatbuffers/flexbuffers.h>

#include <windows.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using namespace wxl::ipc;
using wxl::host::mpq::MpqStore;
namespace wlog = wxl::core::log; // 'log' alone collides with ::log from <cmath>

// Console output is opt-in at runtime via --console; the file log is always on. A runtime gate, not a
// build switch, so a Release host can show the console on demand.
static bool g_console = false;
#define HOST_CONSOLE(...) do { if (g_console) printf(__VA_ARGS__); } while (0)

namespace
{
    std::string g_dataDir;       // host exe folder (defaults to ExeDir; --data overrides)
    DWORD       g_clientPid = 0; // game process to shadow; host exits when it closes
    std::string g_clientRoot;    // client data root (the host runs from <client>/Utils)

    // The serve store, used only by the main thread (no StormLib cross-thread locking). Mounted lazily.
    std::unique_ptr<MpqStore> g_mpq;

    // Large/translated files are served zero-copy: the host copies the bytes once into a named shared
    // section and keeps it alive until the client closes the id.
    struct Blob { HANDLE section; void* view; uint32_t size; };
    std::mutex g_handleMutex;
    std::unordered_map<uint32_t, Blob> g_blobs;
    uint32_t g_nextHandle = 0;

    std::string ExeDir()
    {
        char p[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, p, MAX_PATH);
        std::string s(p, n);
        size_t slash = s.find_last_of("\\/");
        return (slash == std::string::npos) ? std::string(".") : s.substr(0, slash);
    }

    // The host runs from the client's Utils folder, so the client root is its parent.
    std::string ClientRoot()
    {
        std::string root = g_dataDir;
        size_t slash = root.find_last_of("\\/");
        if (slash != std::string::npos) root = root.substr(0, slash);
        return root;
    }

    DWORD WINAPI ClientWatcher(LPVOID clientHandle)
    {
        WaitForSingleObject(static_cast<HANDLE>(clientHandle), INFINITE);
        ExitProcess(0);
        return 0;
    }

    // Copy whole bytes into a fresh named shared section and return its (nonzero) id. The bytes are fully
    // written before the id is returned, so the open response doubles as the ready signal. Returns 0 if the
    // section/view could not be created (caller falls back).
    uint32_t StoreBlob(const std::vector<uint8_t>& bytes)
    {
        uint32_t size = static_cast<uint32_t>(bytes.size());

        std::lock_guard<std::mutex> hl(g_handleMutex);
        uint32_t id = ++g_nextHandle;
        if (id == 0) id = ++g_nextHandle; // ids must be nonzero

        char nm[64];
        BlobName(nm, sizeof(nm), id);
        HANDLE h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size ? size : 1, nm);
        if (!h) return 0;
        void* v = MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, size);
        if (!v) { CloseHandle(h); return 0; }
        if (size) memcpy(v, bytes.data(), size);

        g_blobs.emplace(id, Blob{ h, v, size });
        return id;
    }

    // Build the OpFileOpen response for `bytes`: inline when small, otherwise a zero-copy section id.
    void RespondWithFile(flexbuffers::Builder& fbb, const char* name, std::vector<uint8_t>&& bytes)
    {
        uint32_t size = static_cast<uint32_t>(bytes.size());
        if (size > kInlineMax)
        {
            uint32_t id = StoreBlob(bytes);
            if (id != 0)
            {
                fbb.Vector([&]() { fbb.UInt(StOk); fbb.UInt(id); fbb.UInt(size); });
                HOST_CONSOLE("open  %-44s -> OK id=%u (%u B)\n", name, id, size);
                return;
            }
            fbb.Vector([&]() { fbb.UInt(StNotFound); fbb.UInt(0); fbb.UInt(0); });
            HOST_CONSOLE("open  %-44s -> FAIL (no section, %u B)\n", name, size);
            return;
        }

        // Inline: bytes come back in the open response (one small copy).
        fbb.Vector([&]() { fbb.UInt(StOk); fbb.UInt(0); fbb.UInt(size); fbb.Blob(bytes.data(), bytes.size()); });
        HOST_CONSOLE("open  %-44s -> OK inline (%u B)\n", name, size);
    }

    // Produce the bytes finally served for `name` from the archives: read raw, offer it to the transform
    // hooks (first that reshapes wins), else raw passthrough. Returns false only on a miss (not in archives).
    // Provider hooks are NOT fired here -- the caller fires them first (a provider, e.g. the cache module,
    // short-circuits before any archive read).
    bool ProduceServed(const std::string& name, std::vector<uint8_t>& out)
    {
        std::vector<uint8_t> raw;
        if (!g_mpq->ReadAll(name, raw))
            return false;

        std::vector<uint8_t> reshaped;
        if (wxl::host::Transform(name, raw, reshaped))
        {
            out = std::move(reshaped);
            return true;
        }
        out = std::move(raw);
        return true;
    }

    void HandleFileOpen(flexbuffers::Builder& fbb, const std::string& name)
    {
        std::vector<uint8_t> provided;
        if (wxl::host::Provide(name, provided))
        {
            RespondWithFile(fbb, name.c_str(), std::move(provided));
            return;
        }

        std::vector<uint8_t> served;
        if (!ProduceServed(name, served))
        {
            fbb.Vector([&]() { fbb.UInt(StNotFound); fbb.UInt(0); fbb.UInt(0); });
            HOST_CONSOLE("open  %-44s -> MISS\n", name.c_str());
            return;
        }

        wxl::host::NotifyServed(name, served, wxl::host::ServedOrigin::Client);
        RespondWithFile(fbb, name.c_str(), std::move(served));
    }

    // Fallback range read for a client that cannot map the section directly. The zero-copy client reads the
    // mapping itself and never reaches here.
    void HandleFileRead(flexbuffers::Builder& fbb, const flexbuffers::Vector& vec)
    {
        uint32_t id = vec[1].AsUInt32(), off = vec[2].AsUInt32(), len = vec[3].AsUInt32();
        if (len > kFileChunkMax) len = kFileChunkMax;

        std::vector<uint8_t> out;
        bool found = false;
        {
            std::lock_guard<std::mutex> hl(g_handleMutex);
            auto it = g_blobs.find(id);
            if (it != g_blobs.end())
            {
                found = true;
                const Blob& b = it->second;
                if (off < b.size && b.view)
                {
                    uint32_t avail = b.size - off;
                    uint32_t n = len < avail ? len : avail;
                    const uint8_t* src = static_cast<const uint8_t*>(b.view) + off;
                    out.assign(src, src + n);
                }
            }
        }
        if (found) fbb.Vector([&]() { fbb.UInt(StOk); fbb.Blob(out.data(), out.size()); });
        else       fbb.Vector([&]() { fbb.UInt(StBadRequest); fbb.Blob(nullptr, 0); });
    }

    void HandleFileClose(flexbuffers::Builder& fbb, uint32_t id)
    {
        std::lock_guard<std::mutex> hl(g_handleMutex);
        auto it = g_blobs.find(id);
        if (it != g_blobs.end())
        {
            if (it->second.view)    UnmapViewOfFile(it->second.view);
            if (it->second.section) CloseHandle(it->second.section);
            g_blobs.erase(it);
        }
        fbb.Vector([&]() { fbb.UInt(StOk); });
    }

    // Build the FlexBuffers response for one request payload.
    void ProcessRequest(const std::vector<uint8_t>& req, std::vector<uint8_t>& respOut)
    {
        flexbuffers::Builder fbb;
        if (!req.empty())
        {
            auto vec = flexbuffers::GetRoot(req.data(), req.size()).AsVector();
            uint32_t op = vec[0].AsUInt32();
            switch (op)
            {
            case OpFileOpen:   HandleFileOpen(fbb, vec[1].AsString().str()); break;
            case OpFileRead:   HandleFileRead(fbb, vec); break;
            case OpFileClose:  HandleFileClose(fbb, vec[1].AsUInt32()); break;
            case OpFileExists:
            {
                std::string name = vec[1].AsString().str();
                bool ok = g_mpq->Exists(name) || wxl::host::Exists(name);
                fbb.Vector([&]() { fbb.UInt(ok ? StOk : StNotFound); });
                break;
            }
            default:
                fbb.Vector([&]() { fbb.UInt(StBadRequest); });
                break;
            }
        }
        else
        {
            fbb.Vector([&]() { fbb.UInt(StBadRequest); });
        }

        fbb.Finish();
        const std::vector<uint8_t>& buf = fbb.GetBuffer();
        respOut.assign(buf.begin(), buf.end());
    }

    int Serve()
    {
        // One host per session.
        HANDLE singleton = CreateMutexA(nullptr, FALSE, "Local\\WarcraftXLHostSingleton");
        if (singleton && GetLastError() == ERROR_ALREADY_EXISTS)
        {
            wlog::Printf("host: another instance is running, exiting");
            return 0;
        }

        if (g_clientPid)
        {
            HANDLE client = OpenProcess(SYNCHRONIZE, FALSE, g_clientPid);
            if (client) CreateThread(nullptr, 0, ClientWatcher, client, 0, nullptr);
        }

        g_clientRoot = ClientRoot();
        wxl::host::SetClientRoot(g_clientRoot);

        g_mpq = std::make_unique<MpqStore>();
        g_mpq->Mount(g_clientRoot);

        // List who extended the host (the modules' host faces self-registered before main ran).
        wxl::host::LogRegisteredHandlers();

        if (!wxl::host::ipc::Create())
        {
            wlog::Printf("host: ShmServer.Create failed (err %lu)", GetLastError());
            return 1;
        }

        if (g_console) SetConsoleTitleA("WarcraftXLHost  -  client <-> host IPC");
        HOST_CONSOLE("WarcraftXLHost serving. Waiting for requests...\n\n");
        wlog::Printf("host: serving (%u channel)", kChannels);

        // Single channel: the client serializes its opens, so serve them on this thread.
        std::vector<uint8_t> req, resp;
        for (;;)
        {
            if (!wxl::host::ipc::WaitRequest(0, req)) break;
            ProcessRequest(req, resp);
            wxl::host::ipc::PostResponse(0, resp);
        }
        return 0;
    }
}

int main(int argc, char** argv)
{
    g_dataDir = ExeDir();

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) g_dataDir = argv[++i];
        else if (a == "--client-pid" && i + 1 < argc) g_clientPid = static_cast<DWORD>(strtoul(argv[++i], nullptr, 10));
        else if (a == "--console") g_console = true;
    }

    wlog::Open((g_dataDir + "\\WarcraftXLHost.log").c_str());
    wlog::Printf("WarcraftXLHost starting (build %s %s)", __DATE__, __TIME__);
    int rc = Serve();
    wlog::Close();
    return rc;
}
