// WarcraftXLHost.exe (64-bit) entry point: parses config, then runs the serve loop or an offline dump.
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

#include "Console.hpp"
#include "Host.hpp"
#include "Produce.hpp"
#include "Profile.hpp"
#include "Serve.hpp"
#include "mpq/MpqStore.hpp"
#include "common/Config.hpp"
#include "common/Log.hpp"

#include <windows.h>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using wxl::host::mpq::MpqStore;
namespace hprof = wxl::host::profile;

namespace
{
    std::string g_dataDir; // host data folder (defaults to ExeDir; --data overrides)

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

    /** @brief Reports whether the per-open console line was requested by env var or the client-root flag file. */
    bool ConsoleOpenLogRequested()
    {
        if (wxl::config::Env("WXL_HOST_CONSOLE_OPENS", false)) return true;
        const std::string root = ClientRoot();
        if (root.empty()) return false;
        return GetFileAttributesA((root + "\\WarcraftXLConsoleOpens.flag").c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    /**
     * @brief Resolves one asset through the serve pipeline and writes the bytes to a file.
     *        Reuses the process-wide host state, so a later call in the same process (e.g. a
     *        texture request after its model) sees whatever the earlier call registered.
     * @param name  asset path as the client would request it (lowercase backslash form)
     * @param dest  output file receiving the served bytes
     * @return process exit code (0 = served and written)
     */
    int ProvideToFile(const std::string& name, const std::string& dest)
    {
        std::vector<uint8_t> out;
        hprof::OpenTrace trace;
        if (!wxl::host::Provide(name, out) && !wxl::host::produce::ProduceServed(name, out, trace))
        {
            WLOG_INFO("provide: %s -> MISS", name.c_str());
            fprintf(stderr, "MISS %s\n", name.c_str());
            return 1;
        }

        FILE* f = fopen(dest.c_str(), "wb");
        if (!f)
        {
            fprintf(stderr, "cannot write %s\n", dest.c_str());
            return 2;
        }
        fwrite(out.data(), 1, out.size(), f);
        fclose(f);
        WLOG_INFO("provide: %s -> %u bytes -> %s", name.c_str(),
                     static_cast<uint32_t>(out.size()), dest.c_str());
        printf("OK %u bytes -> %s\n", static_cast<uint32_t>(out.size()), dest.c_str());
        return 0;
    }

    /**
     * @brief Runs the repeated --provide/--out dumps in one process so later requests see earlier state.
     * @param clientRoot  client data root to mount
     * @param provides    (name, out) pairs to resolve in order
     * @return process exit code (0 = all provides written)
     */
    int RunProvides(const std::string& clientRoot,
                    const std::vector<std::pair<std::string, std::string>>& provides)
    {
        wxl::host::produce::ForceServeNative();
        wxl::host::SetClientRoot(clientRoot);
        auto mpq = std::make_unique<MpqStore>();
        mpq->Mount(clientRoot);
        wxl::host::produce::SetMpqStore(std::move(mpq));
        wxl::host::LogRegisteredHandlers();

        int rc = 0;
        for (const auto& [name, dest] : provides)
        {
            rc = ProvideToFile(name, dest);
            if (rc) break;
        }
        return rc;
    }
}

/**
 * @brief Parses command-line arguments, opens the log, and runs the serve loop.
 * @param argc  argument count
 * @param argv  argument values (--data, --client-pid, --console,
 *              --provide NAME --out FILE [--provide NAME2 --out FILE2 ...])
 *              repeated --provide/--out pairs run in the same process, in order, so a later
 *              request (e.g. a texture) sees state an earlier one (e.g. its model) registered;
 *              --console-opens enables per-open console output
 * @return process exit code
 */
int main(int argc, char** argv)
{
    g_dataDir = ExeDir();

    DWORD clientPid = 0; // client process to shadow; the host exits when it closes
    bool console = false;
    bool consoleOpenLog = false;
    std::vector<std::pair<std::string, std::string>> provides; // (name, out)
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) g_dataDir = argv[++i];
        else if (a == "--client-pid" && i + 1 < argc) clientPid = static_cast<DWORD>(strtoul(argv[++i], nullptr, 10));
        else if (a == "--console") console = true;
        else if (a == "--provide" && i + 1 < argc) provides.push_back({ argv[++i], "provided.bin" });
        else if (a == "--out" && i + 1 < argc && !provides.empty()) provides.back().second = argv[++i];
        else if (a == "--console-opens") consoleOpenLog = true;
    }
    if (!consoleOpenLog)
        consoleOpenLog = ConsoleOpenLogRequested();
    wxl::host::EnableConsole(console);
    wxl::host::EnableConsoleOpenLog(consoleOpenLog);

    wxl::log::Open((g_dataDir + "\\WarcraftXLHost.log").c_str());
    WLOG_INFO("WarcraftXLHost starting (build %s %s)", __DATE__, __TIME__);
    hprof::LogSettings();
    WLOG_INFO("host: transform cache limit=%zu MB entry=%zu MB consoleOpenLog=%u serveNative=%u",
                 wxl::host::produce::TransformCacheMaxBytes() / (1024 * 1024),
                 wxl::host::produce::TransformCacheMaxEntry() / (1024 * 1024),
                 consoleOpenLog ? 1u : 0u,
                 wxl::host::produce::ServeNativeArchives() ? 1u : 0u);

    int rc = provides.empty()
        ? wxl::host::serve::Run(ClientRoot(), static_cast<uint32_t>(clientPid))
        : RunProvides(ClientRoot(), provides);

    wxl::log::Close();
    return rc;
}
