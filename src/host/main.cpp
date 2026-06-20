// WarcraftXLHost.exe (64-bit) entry point: archive owner and IPC transport that serves files to the client.
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

// Console output is opt-in at runtime via --console; the file log is always on.
static bool g_console = false;
#define HOST_CONSOLE(...) do { if (g_console) printf(__VA_ARGS__); } while (0)

namespace
{
    std::string g_dataDir;       // host data folder (defaults to ExeDir; --data overrides)
    DWORD       g_clientPid = 0; // client process to shadow; host exits when it closes
    std::string g_clientRoot;    // client data root (the host runs from the client Utils folder)

    // Serve store, used only by the main thread (StormLib handles are single-thread). Mounted lazily.
    std::unique_ptr<MpqStore> g_mpq;

    // Large files are served zero-copy: the host copies the bytes once into a named shared section and
    // keeps it alive until the client closes the id.

    /** @brief Holds a shared section serving one file's bytes zero-copy to the client. */
    struct Blob { HANDLE section; void* view; uint32_t size; };
    std::mutex g_handleMutex;
    std::unordered_map<uint32_t, Blob> g_blobs;
    uint32_t g_nextHandle = 0;

    /**
     * @brief Returns the folder containing the host executable.
     * @return host executable folder, or "." if it cannot be determined
     */
    std::string ExeDir()
    {
        char p[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, p, MAX_PATH);
        std::string s(p, n);
        size_t slash = s.find_last_of("\\/");
        return (slash == std::string::npos) ? std::string(".") : s.substr(0, slash);
    }

    /**
     * @brief Returns the client data root, the parent of the host data folder.
     * @return client data root path
     */
    std::string ClientRoot()
    {
        std::string root = g_dataDir;
        size_t slash = root.find_last_of("\\/");
        if (slash != std::string::npos) root = root.substr(0, slash);
        return root;
    }

    /**
     * @brief Waits on the client process handle and exits the host when the client closes.
     * @param clientHandle  HANDLE to the client process, passed as the thread parameter
     * @return thread exit code (unreached)
     */
    DWORD WINAPI ClientWatcher(LPVOID clientHandle)
    {
        WaitForSingleObject(static_cast<HANDLE>(clientHandle), INFINITE);
        ExitProcess(0);
        return 0;
    }

    /**
     * @brief Copies whole bytes into a fresh named shared section and returns its nonzero id.
     * @param bytes  file bytes to place in the section
     * @return the nonzero blob id, or 0 if the section or view could not be created
     */
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

    /**
     * @brief Builds the file-open response: inline bytes when small, otherwise a zero-copy section id.
     * @param fbb    FlexBuffers builder receiving the response
     * @param name   file name (logging)
     * @param bytes  the file bytes to return
     */
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

        // Inline: bytes come back in the open response.
        fbb.Vector([&]() { fbb.UInt(StOk); fbb.UInt(0); fbb.UInt(size); fbb.Blob(bytes.data(), bytes.size()); });
        HOST_CONSOLE("open  %-44s -> OK inline (%u B)\n", name, size);
    }

    /**
     * @brief Reads raw archive bytes for `name`, offers them to the transform hooks, else passes them through.
     * @param name  archive-internal file name
     * @param out   receives the reshaped or raw bytes
     * @return false only on an archive miss; provider hooks are fired by the caller, not here
     */
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

    /**
     * @brief Serves a file-open request: tries the providers, then the archive read and transforms.
     * @param fbb   FlexBuffers builder receiving the response
     * @param name  file name requested
     */
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

        wxl::host::NotifyServed(name, served);
        RespondWithFile(fbb, name.c_str(), std::move(served));
    }

    /**
     * @brief Serves a fallback range read for a client that cannot map the blob section directly.
     * @param fbb  FlexBuffers builder receiving the response
     * @param vec  request vector holding blob id, offset, and length
     */
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

    /**
     * @brief Serves a file-close request: unmaps and releases the blob section for `id`.
     * @param fbb  FlexBuffers builder receiving the response
     * @param id   blob id to release
     */
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

    /**
     * @brief Decodes one request payload and builds the FlexBuffers response.
     * @param req      request payload bytes
     * @param respOut  receives the response payload bytes
     */
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

    /**
     * @brief Mounts the archives, creates the transport, and runs the request loop until the client closes.
     * @return process exit code
     */
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

        // List the registered hooks (module host faces self-registered before main ran).
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
        std::vector<uint8_t> req, resp; // request and response payload buffers, reused each iteration
        for (;;)
        {
            if (!wxl::host::ipc::WaitRequest(0, req)) break;
            ProcessRequest(req, resp);
            wxl::host::ipc::PostResponse(0, resp);
        }
        return 0;
    }
}

/**
 * @brief Parses command-line arguments, opens the log, and runs the serve loop.
 * @param argc  argument count
 * @param argv  argument values (--data, --client-pid, --console)
 * @return process exit code
 */
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
