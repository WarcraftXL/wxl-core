// Serve subsystem: mounts the archives, creates the transport, and runs the request loop.
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

#include "Serve.hpp"

#include "Blobs.hpp"
#include "Console.hpp"
#include "Host.hpp"
#include "Produce.hpp"
#include "Profile.hpp"
#include "Warm.hpp"
#include "ipc/Protocol.hpp"
#include "ipc/ShmServer.hpp"
#include "mpq/MpqStore.hpp"
#include "common/Config.hpp"
#include "common/Log.hpp"

#include <flatbuffers/flexbuffers.h>

#include <windows.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace wxl::ipc;
using wxl::host::mpq::MpqStore;
namespace hprof = wxl::host::profile;

namespace wxl::host::serve
{
    namespace
    {
        // g_profileMutex serializes only the periodic profiler bookkeeping; archive reads and transforms
        // remain fully concurrent across the worker pool.
        std::mutex g_profileMutex;

        /**
         * @brief Picks the worker-thread count: channels - 1, overridable via WXL_HOST_WORKERS.
         *
         * Channels bound how many requests the client can have in flight; workers bound how many run at
         * once. channels/2 halved throughput exactly when the client bursts (camera pans, tile entry):
         * half the inflight opens queued behind the other half while the render thread waited on one of
         * them. channels-1 keeps one logical core of headroom for the game client; most request time is
         * I/O wait anyway, not CPU. WXL_HOST_WORKERS overrides for low-core machines or experiments.
         * @param channelCount  channel count chosen by wxl::host::ipc::Create()
         * @return worker thread count, at least 1, at most channelCount
         */
        uint32_t ComputeWorkerCount(uint32_t channelCount)
        {
            const uint64_t requested = wxl::config::U64("WXL_HOST_WORKERS", 0, 0, 16);
            if (requested)
                return static_cast<uint32_t>(requested < channelCount ? requested : channelCount);
            const uint32_t workers = channelCount ? channelCount - 1 : 1;
            return workers ? workers : 1;
        }

        /**
         * @brief Waits on the client process handle and exits the host when the client closes.
         * @param clientHandle  HANDLE to the client process, passed as the thread parameter
         * @return thread exit code (unreached)
         */
        DWORD WINAPI ClientWatcher(LPVOID clientHandle)
        {
            WaitForSingleObject(static_cast<HANDLE>(clientHandle), INFINITE);
            // The watcher terminates the process directly, bypassing main()/Close(). Preserve buffered
            // startup and the most recent completed profile window before the OS tears down the CRT.
            wxl::log::Flush();
            ExitProcess(0);
            return 0;
        }

        /**
         * @brief Builds the file-open response: inline bytes when small, otherwise a zero-copy section id.
         * @param fbb    FlexBuffers builder receiving the response
         * @param name   file name (logging)
         * @param bytes  the file bytes to return
         * @param trace  per-open counters and stage timings
         */
        void RespondWithFile(flexbuffers::Builder& fbb, const char* name, std::vector<uint8_t>&& bytes,
                             hprof::OpenTrace& trace)
        {
            uint32_t size = static_cast<uint32_t>(bytes.size());
            if (size > kInlineMax)
            {
                const uint64_t started = hprof::Now();
                bool reused = false;
                uint32_t id = blobs::Store(name ? std::string(name) : std::string(), bytes, reused);
                trace.blobTicks += hprof::Now() - started;
                if (id != 0)
                {
                    trace.ok = true;
                    trace.bytes = size;
                    trace.sharedBytes += size;
                    if (reused) ++trace.blobReuses;
                    else        ++trace.blobCreates;
                    fbb.Vector([&]() { fbb.UInt(StOk); fbb.UInt(id); fbb.UInt(size); });
                    HOST_OPEN_CONSOLE("open  %-44s -> OK id=%u (%u B)\n", name, id, size);
                    return;
                }
                ++trace.blobFailures;
                fbb.Vector([&]() { fbb.UInt(StNotFound); fbb.UInt(0); fbb.UInt(0); });
                HOST_OPEN_CONSOLE("open  %-44s -> FAIL (no section, %u B)\n", name, size);
                return;
            }

            // Inline: bytes come back in the open response.
            trace.ok = true;
            trace.bytes = size;
            trace.inlineBytes += size;
            ++trace.inlineResponses;
            fbb.Vector([&]() { fbb.UInt(StOk); fbb.UInt(0); fbb.UInt(size); fbb.Blob(bytes.data(), bytes.size()); });
            HOST_OPEN_CONSOLE("open  %-44s -> OK inline (%u B)\n", name, size);
        }

        /**
         * @brief Serves a file-open request: tries the providers, then the archive read and transforms.
         * @param fbb   FlexBuffers builder receiving the response
         * @param name  file name requested
         * @param trace receives the open-stage measurements and outcome
         */
        void HandleFileOpen(flexbuffers::Builder& fbb, const std::string& name, hprof::OpenTrace& trace)
        {
            std::vector<uint8_t> provided;
            ++trace.providerCalls;
            const uint64_t providerStarted = hprof::Now();
            const bool providerHit = wxl::host::Provide(name, provided);
            trace.providerTicks += hprof::Now() - providerStarted;
            if (providerHit)
            {
                ++trace.providerHits;
                RespondWithFile(fbb, name.c_str(), std::move(provided), trace);
                return;
            }

            std::vector<uint8_t> served;
            if (!produce::ProduceServed(name, served, trace))
            {
                trace.miss = true;
                fbb.Vector([&]() { fbb.UInt(StNotFound); fbb.UInt(0); fbb.UInt(0); });
                HOST_OPEN_CONSOLE("open  %-44s -> MISS\n", name.c_str());
                return;
            }

            ++trace.servedCalls;
            const uint64_t servedStarted = hprof::Now();
            wxl::host::NotifyServed(name, served);
            trace.servedTicks += hprof::Now() - servedStarted;
            RespondWithFile(fbb, name.c_str(), std::move(served), trace);
        }

        /**
         * @brief Serves a fallback range read for a client that cannot map the blob section directly.
         * @param fbb  FlexBuffers builder receiving the response
         * @param vec  request vector holding blob id, offset, and length
         * @param trace receives transfer bytes and invalid-id status
         */
        void HandleFileRead(flexbuffers::Builder& fbb, const flexbuffers::Vector& vec,
                            hprof::RequestTrace& trace)
        {
            uint32_t id = vec[1].AsUInt32(), off = vec[2].AsUInt32(), len = vec[3].AsUInt32();
            if (len > kFileChunkMax) len = kFileChunkMax;

            std::vector<uint8_t> out;
            const bool found = blobs::Read(id, off, len, out);
            trace.ok = found;
            trace.badRequest = !found;
            trace.bytes = out.size();
            if (found) fbb.Vector([&]() { fbb.UInt(StOk); fbb.Blob(out.data(), out.size()); });
            else       fbb.Vector([&]() { fbb.UInt(StBadRequest); fbb.Blob(nullptr, 0); });
        }

        /**
         * @brief Serves a file-close request: unmaps and releases the blob section for `id`.
         * @param fbb  FlexBuffers builder receiving the response
         * @param id   blob id to release
         */
        void HandleFileClose(flexbuffers::Builder& fbb, uint32_t id, hprof::RequestTrace& trace)
        {
            trace.ok = true;
            blobs::Close(id);
            fbb.Vector([&]() { fbb.UInt(StOk); });
        }

        /**
         * @brief Decodes one request payload and builds the FlexBuffers response.
         * @param req    request payload bytes
         * @param fbb    fresh builder that receives the finished response; the caller posts
         *               fbb.GetBuffer() directly so the payload is never copied host-side
         * @param trace  receives decoded operation, outcome, and open-stage data
         */
        void ProcessRequest(const std::vector<uint8_t>& req, flexbuffers::Builder& fbb,
                            hprof::RequestTrace& trace)
        {
            if (!req.empty())
            {
                auto vec = flexbuffers::GetRoot(req.data(), req.size()).AsVector();
                uint32_t op = vec[0].AsUInt32();
                switch (op)
                {
                case OpFileOpen:
                    trace.op = hprof::RequestOp::FileOpen;
                    trace.name = vec[1].AsString().str();
                    HandleFileOpen(fbb, trace.name, trace.open);
                    trace.ok = trace.open.ok;
                    trace.bytes = trace.open.bytes;
                    break;
                case OpFileRead:
                    trace.op = hprof::RequestOp::FileRead;
                    HandleFileRead(fbb, vec, trace);
                    break;
                case OpFileClose:
                    trace.op = hprof::RequestOp::FileClose;
                    HandleFileClose(fbb, vec[1].AsUInt32(), trace);
                    break;
                case OpFileExists:
                {
                    trace.op = hprof::RequestOp::FileExists;
                    std::string name = vec[1].AsString().str();
                    bool ok = produce::ArchiveExists(name) || wxl::host::Exists(name);
                    trace.ok = ok;
                    fbb.Vector([&]() { fbb.UInt(ok ? StOk : StNotFound); });
                    break;
                }
                case OpResolveFdid:
                {
                    trace.op = hprof::RequestOp::ResolveFdid;
                    const uint32_t fileDataId = vec[1].AsUInt32();
                    std::string path;
                    const bool ok = wxl::host::ResolveFdid(fileDataId, path);
                    trace.ok = ok;
                    if (ok) fbb.Vector([&]() { fbb.UInt(StOk); fbb.String(path); });
                    else    fbb.Vector([&]() { fbb.UInt(StNotFound); });
                    break;
                }
                default:
                    trace.badRequest = true;
                    fbb.Vector([&]() { fbb.UInt(StBadRequest); });
                    break;
                }
            }
            else
            {
                trace.badRequest = true;
                fbb.Vector([&]() { fbb.UInt(StBadRequest); });
            }

            fbb.Finish();
        }

        /** @brief Samples transform-cache and live shared-section memory for a periodic profile report. */
        hprof::Gauges SnapshotProfileGauges()
        {
            hprof::Gauges gauges;
            produce::SnapshotGauges(gauges.transformCacheEntries, gauges.transformCacheBytes);
            blobs::SnapshotGauges(gauges.blobs, gauges.blobBytes, gauges.blobRefs);
            return gauges;
        }
    }

    int Run(const std::string& clientRoot, uint32_t clientPid)
    {
        // One host per session.
        HANDLE singleton = CreateMutexA(nullptr, FALSE, "Local\\WarcraftXLHostSingleton");
        if (singleton && GetLastError() == ERROR_ALREADY_EXISTS)
        {
            WLOG_INFO("host: another instance is running, exiting");
            return 0;
        }

        if (clientPid)
        {
            HANDLE client = OpenProcess(SYNCHRONIZE, FALSE, clientPid);
            if (client) CreateThread(nullptr, 0, ClientWatcher, client, 0, nullptr);
        }

        wxl::host::SetClientRoot(clientRoot);

        auto mpq = std::make_unique<MpqStore>();
        mpq->Mount(clientRoot);
        produce::SetMpqStore(std::move(mpq));

        // List the registered hooks (module host faces self-registered before main ran).
        wxl::host::LogRegisteredHandlers();

        warm::StartResolverWarmer();
        warm::StartTileWarmer();

        if (!wxl::host::ipc::Create())
        {
            WLOG_INFO("host: ShmServer.Create failed (err %lu)", GetLastError());
            return 1;
        }

        if (ConsoleEnabled()) SetConsoleTitleA("WarcraftXLHost  -  client <-> host IPC");
        HOST_CONSOLE("WarcraftXLHost serving. Waiting for requests...\n\n");
        const uint32_t channels = wxl::host::ipc::ChannelCount(); // chosen by Create() from hardware_concurrency
        const uint32_t workerCount = ComputeWorkerCount(channels);
        WLOG_INFO("host: serving (%u channels, %u worker thread(s))", channels, workerCount);

        // Fewer worker threads than channels: channels bound how many requests the client can have in
        // flight at once, workers bound how many Transforms actually run concurrently on this machine's
        // cores. Every worker waits on the full channel set (WaitAnyRequest) and picks up whichever one
        // is free, rather than one thread being tied to one channel -- so a burst larger than the worker
        // count still queues on the (idle, cheap) channel events instead of spinning up one CPU-bound
        // thread per logical core and starving the game client running on the same machine.
        auto worker = []() {
            std::vector<uint8_t> req;
            for (;;)
            {
                uint32_t ch = 0, reqSeq = 0;
                if (!wxl::host::ipc::WaitAnyRequest(ch, reqSeq, req)) break;
                hprof::RequestTrace trace;
                const uint64_t requestStarted = hprof::Now();
                flexbuffers::Builder fbb;
                ProcessRequest(req, fbb, trace);
                const uint64_t postStarted = hprof::Now();
                wxl::host::ipc::PostResponse(ch, reqSeq, fbb.GetBuffer());
                const uint64_t requestFinished = hprof::Now();

                // Profile.cpp keeps a compact non-atomic window. Serialize only its bookkeeping; archive
                // reads and transforms remain fully concurrent across the worker pool.
                std::lock_guard<std::mutex> profileLock(g_profileMutex);
                hprof::RecordRequest(trace, requestFinished - requestStarted,
                                     requestFinished - postStarted);
                if (hprof::ReportDue())
                {
                    hprof::Report(SnapshotProfileGauges());
                    wxl::host::LogAndResetHandlerProfile();
                }
            }
        };

        std::vector<std::thread> workers;
        for (uint32_t w = 1; w < workerCount; ++w)
            workers.emplace_back(worker);

        worker(); // this thread is also a pool worker; its loop only ends at process teardown
        for (auto& t : workers) t.join();
        return 0;
    }
}
