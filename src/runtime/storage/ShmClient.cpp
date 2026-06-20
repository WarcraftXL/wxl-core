// DLL IPC client: launch + connect to the asset host, run file ops over the shared-memory mailbox.
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

#include "runtime/storage/ShmClient.hpp"

#include "events/Event.hpp"
#include "host/ipc/Protocol.hpp"

#include <flatbuffers/flexbuffers.h>

#include <windows.h>
#include <atomic>
#include <mutex>

using namespace wxl::ipc;

namespace
{
    // --- connection state (set once at Connect, read-only afterwards) ---
    std::mutex g_connectMutex;          // guards the one-time connect/disconnect of the shared objects
    std::atomic<bool> g_connected{ false };
    HANDLE g_shm = nullptr;
    uint8_t* g_base = nullptr;
    HANDLE g_reqEvent[kChannels]  = {};
    HANDLE g_respEvent[kChannels] = {};

    // --- channel pool: a free channel is acquired per request, then released ---
    std::atomic<bool> g_channelBusy[kChannels] = {}; // false = free

    constexpr uint32_t kRequestTimeoutMs = 2000;

    /**
     * @brief Returns the directory of this module, i.e. the client root.
     * @return the module directory, or "." when it cannot be resolved.
     */
    std::string ModuleDir()
    {
        HMODULE hm = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&ModuleDir), &hm);
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(hm, path, MAX_PATH);
        std::string s(path, n);
        size_t slash = s.find_last_of("\\/");
        return (slash == std::string::npos) ? std::string(".") : s.substr(0, slash);
    }

    /**
     * @brief Closes the shared section and every event pair. Caller holds g_connectMutex.
     */
    void DisconnectLocked()
    {
        g_connected.store(false);
        for (uint32_t i = 0; i < kChannels; ++i)
        {
            if (g_reqEvent[i])  { CloseHandle(g_reqEvent[i]);  g_reqEvent[i]  = nullptr; }
            if (g_respEvent[i]) { CloseHandle(g_respEvent[i]); g_respEvent[i] = nullptr; }
        }
        if (g_base) { UnmapViewOfFile(g_base); g_base = nullptr; }
        if (g_shm)  { CloseHandle(g_shm); g_shm = nullptr; }
    }

    /**
     * @brief Opens the shared window and all channel event pairs once. Guarded by g_connectMutex.
     * @return true when connected (or already connected).
     */
    bool ConnectInner()
    {
        if (g_connected.load()) return true;
        std::lock_guard<std::mutex> lock(g_connectMutex);
        if (g_connected.load()) return true;

        g_shm = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, kShmName);
        if (!g_shm) return false;
        g_base = static_cast<uint8_t*>(MapViewOfFile(g_shm, FILE_MAP_ALL_ACCESS, 0, 0, kShmSize));
        if (!g_base) { CloseHandle(g_shm); g_shm = nullptr; return false; }

        // The host stamps magic/version into channel 0's header.
        auto* hdr0 = ChannelHeader(g_base, 0);
        if (hdr0->magic != kMagic || hdr0->version != kVersion)
        {
            UnmapViewOfFile(g_base); g_base = nullptr;
            CloseHandle(g_shm); g_shm = nullptr;
            return false;
        }

        for (uint32_t i = 0; i < kChannels; ++i)
        {
            char rn[64], sn[64];
            ReqEventName(rn, sizeof(rn), i);
            RespEventName(sn, sizeof(sn), i);
            g_reqEvent[i]  = OpenEventA(EVENT_ALL_ACCESS, FALSE, rn);
            g_respEvent[i] = OpenEventA(EVENT_ALL_ACCESS, FALSE, sn);
            if (!g_reqEvent[i] || !g_respEvent[i]) { DisconnectLocked(); return false; }
        }

        g_connected.store(true);
        return true;
    }

    /**
     * @brief Acquires a free channel index, yielding while the pool is full.
     * @return the acquired channel index.
     */
    uint32_t AcquireChannel()
    {
        for (;;)
        {
            for (uint32_t i = 0; i < kChannels; ++i)
            {
                bool expected = false;
                if (g_channelBusy[i].compare_exchange_strong(expected, true,
                        std::memory_order_acquire, std::memory_order_relaxed))
                    return i;
            }
            SwitchToThread();
        }
    }

    /**
     * @brief Releases a channel back to the pool.
     * @param i  channel index to free.
     */
    void ReleaseChannel(uint32_t i)
    {
        g_channelBusy[i].store(false, std::memory_order_release);
    }

    /**
     * @brief Runs one request on a channel: write payload, bump reqSeq, signal, wait for response.
     * @param ch   channel index.
     * @param req  request payload.
     * @return true when a response arrives before the timeout.
     */
    bool SendOnChannel(uint32_t ch, const std::vector<uint8_t>& req)
    {
        if (req.size() > kChannelPayload) return false;
        auto* hdr = ChannelHeader(g_base, ch);
        uint8_t* payload = ChannelPayload(g_base, ch);
        memcpy(payload, req.data(), req.size());
        hdr->reqLen = static_cast<uint32_t>(req.size());
        ++hdr->reqSeq;
        SetEvent(g_reqEvent[ch]);
        return WaitForSingleObject(g_respEvent[ch], kRequestTimeoutMs) == WAIT_OBJECT_0;
    }

    /**
     * @brief Runs a full request round-trip: connect, acquire a channel, send, parse the response.
     *
     * The response is parsed while the channel is still held, since the payload lives in the shared
     * window and is reused once released.
     * @param req         request payload.
     * @param onResponse  callback invoked with the response vector on success.
     * @return true when the request completed.
     */
    template <class Fn>
    bool Transact(const std::vector<uint8_t>& req, Fn&& onResponse)
    {
        if (!ConnectInner()) return false;
        uint32_t ch = AcquireChannel();
        bool ok = SendOnChannel(ch, req);
        if (ok)
        {
            auto* hdr = ChannelHeader(g_base, ch);
            const uint8_t* payload = ChannelPayload(g_base, ch);
            if (hdr->respLen && hdr->respLen <= kChannelPayload)
                onResponse(flexbuffers::GetRoot(payload, hdr->respLen).AsVector());
        }
        ReleaseChannel(ch);
        return ok;
    }
}

namespace wxl::runtime::ipc
{
    /**
     * @brief Launches the asset host if not already running, after firing OnBeforeHostLaunch.
     */
    void EnsureHostRunning()
    {
        HANDLE existing = OpenFileMappingA(FILE_MAP_READ, FALSE, kShmName);
        if (existing) { CloseHandle(existing); return; }

        std::string root = ModuleDir();
        std::string dir = root + "\\Utils";
        std::string exe = dir + "\\WarcraftXLHost.exe";

        // Let a module observe / veto the launch (e.g. it manages the host itself).
        bool cancel = false;
        wxl::events::HostLaunchArgs a{ exe.c_str(), &cancel };
        wxl::events::Emit(wxl::events::Event::OnBeforeHostLaunch, &a);
        if (cancel) return;

        // A "WarcraftXLConsole.flag" file next to the client turns the host console on (opt-in at runtime).
        bool console = GetFileAttributesA((root + "\\WarcraftXLConsole.flag").c_str()) != INVALID_FILE_ATTRIBUTES;

        // --client-pid lets the host exit when this client closes; --console enables its console output.
        char cmd[160];
        wsprintfA(cmd, "WarcraftXLHost.exe --client-pid %lu%s", GetCurrentProcessId(), console ? " --console" : "");

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        DWORD creationFlags = console ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW;
        if (CreateProcessA(exe.c_str(), cmd, nullptr, nullptr, FALSE,
                           creationFlags, nullptr, dir.c_str(), &si, &pi))
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    /**
     * @brief Polls for the host mailbox until it appears or the timeout elapses.
     * @param timeoutMs  maximum wait in milliseconds.
     * @return true if the host mailbox appeared.
     */
    bool WaitForHost(uint32_t timeoutMs)
    {
        for (uint32_t waited = 0; waited < timeoutMs; waited += 50)
        {
            HANDLE h = OpenFileMappingA(FILE_MAP_READ, FALSE, kShmName);
            if (h) { CloseHandle(h); return true; }
            Sleep(50);
        }
        return false;
    }

    /** @brief Opens or reopens the host mailbox. @return true if connected. */
    bool Connect()     { return ConnectInner(); }
    /** @brief Reports whether the host mailbox is connected. @return true while connected. */
    bool IsConnected() { return g_connected.load(); }

    /**
     * @brief Opens a file from the host, requesting inline bytes or a shared section.
     * @param name   archive-relative file name.
     * @param flags  native open flags.
     * @return the open result.
     */
    FileOpenResult FileOpen(const std::string& name, uint32_t flags)
    {
        flexbuffers::Builder fbb;
        fbb.Vector([&]() { fbb.UInt(OpFileOpen); fbb.String(name); fbb.UInt(flags); });
        fbb.Finish();

        FileOpenResult r{ false, 0, 0, {} };
        Transact(fbb.GetBuffer(), [&](const flexbuffers::Vector& vec) {
            if (vec[0].AsUInt32() != StOk) return;
            r = { true, vec[1].AsUInt32(), vec[2].AsUInt32(), {} };
            if (r.id == 0 && vec.size() > 3) // inline: copy bytes out of the shared window
            {
                auto blob = vec[3].AsBlob();
                r.inlineData.assign(blob.data(), blob.data() + blob.size());
            }
        });
        return r;
    }

    /**
     * @brief Maps the host blob section for an id read-only.
     * @param id         host blob id.
     * @param size       section size to map.
     * @param outView    receives the mapped view.
     * @param outHandle  receives the section handle.
     * @return true on success.
     */
    bool MapBlob(uint32_t id, uint32_t size, void*& outView, void*& outHandle)
    {
        char nm[64];
        BlobName(nm, sizeof(nm), id);
        HANDLE h = OpenFileMappingA(FILE_MAP_READ, FALSE, nm);
        if (!h) return false;
        void* v = MapViewOfFile(h, FILE_MAP_READ, 0, 0, size);
        if (!v) { CloseHandle(h); return false; }
        outView = v;
        outHandle = h;
        return true;
    }

    /**
     * @brief Releases a mapping from MapBlob (null-safe).
     * @param view    mapped view.
     * @param handle  section handle.
     */
    void UnmapBlob(void* view, void* handle)
    {
        if (view)   UnmapViewOfFile(view);
        if (handle) CloseHandle(static_cast<HANDLE>(handle));
    }

    /**
     * @brief Reads up to cap bytes at an offset into dst in one round trip.
     * @param id   host file id.
     * @param off  byte offset to read from.
     * @param dst  destination buffer.
     * @param cap  maximum bytes to copy (clamped to kFileChunkMax).
     * @return bytes copied.
     */
    uint32_t FileReadChunk(uint32_t id, uint32_t off, void* dst, uint32_t cap)
    {
        if (cap > kFileChunkMax) cap = kFileChunkMax;

        flexbuffers::Builder fbb;
        fbb.Vector([&]() { fbb.UInt(OpFileRead); fbb.UInt(id); fbb.UInt(off); fbb.UInt(cap); });
        fbb.Finish();

        uint32_t n = 0;
        Transact(fbb.GetBuffer(), [&](const flexbuffers::Vector& vec) {
            if (vec[0].AsUInt32() != StOk) return;
            auto blob = vec[1].AsBlob();
            n = static_cast<uint32_t>(blob.size());
            if (n > cap) n = cap;
            memcpy(dst, blob.data(), n);
        });
        return n;
    }

    /**
     * @brief Releases a host file id (fire-and-forget).
     * @param id  host file id.
     */
    void FileClose(uint32_t id)
    {
        flexbuffers::Builder fbb;
        fbb.Vector([&]() { fbb.UInt(OpFileClose); fbb.UInt(id); });
        fbb.Finish();
        Transact(fbb.GetBuffer(), [](const flexbuffers::Vector&) {}); // fire-and-forget release
    }

    /**
     * @brief Tests whether a file exists in the host archive set.
     * @param name  archive-relative file name.
     * @return true if the host reports the file present.
     */
    bool FileExists(const std::string& name)
    {
        flexbuffers::Builder fbb;
        fbb.Vector([&]() { fbb.UInt(OpFileExists); fbb.String(name); });
        fbb.Finish();

        bool exists = false;
        Transact(fbb.GetBuffer(), [&](const flexbuffers::Vector& vec) {
            exists = vec[0].AsUInt32() == StOk;
        });
        return exists;
    }
}
