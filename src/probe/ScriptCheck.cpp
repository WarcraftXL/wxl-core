// wxl-script-check.exe: drives wxl/Script.hpp against a fake service table. A script deriving two
// types is added, every hook of both types is subscribed, and an emitted event reaches the override
// with its args unpacked. Exit code 0 means every check passed. Development only, never deployed.
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

#include "wxl/Script.hpp"
#include "wxl/game/Objects.hpp"

#include <cstdio>
#include <cstring>

namespace
{
    int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

    struct Sub { uint32_t event; WXL_EventFn fn; void* user; };
    Sub      g_subs[64];
    uint32_t g_subCount = 0;
    char     g_lastLog[256];

    void __cdecl FakeLog(int, const char* tag, const char* fmt, ...)
    {
        va_list args; va_start(args, fmt);
        char line[200]; std::vsnprintf(line, sizeof line, fmt, args); va_end(args);
        std::snprintf(g_lastLog, sizeof g_lastLog, "%s: %s", tag, line);
    }
    void __cdecl FakeSubscribe(uint32_t event, WXL_EventFn fn, void* user)
    {
        if (g_subCount < 64) g_subs[g_subCount++] = { event, fn, user };
    }
    void __cdecl FakeEmit(uint32_t event, const void* args)
    {
        for (uint32_t i = 0; i < g_subCount; ++i) if (g_subs[i].event == event) g_subs[i].fn(g_subs[i].user, args);
    }
    void* __cdecl FakeGetInterface(const char*, uint32_t) { return nullptr; }

    class Both final : public wxl::WorldScript, public wxl::RenderScript
    {
    public:
        Both() { SetName("both"); }
        void OnUpdate(float dt, uint32_t timeMs) override { lastDt = dt; lastTime = timeMs; ++updates; }
        void OnInput(uint32_t message, uintptr_t, uintptr_t, bool& handled) override { if (message == 7) handled = true; }
        void OnEndScene(void* device) override { lastDevice = device; }
        float lastDt = 0; uint32_t lastTime = 0; int updates = 0; void* lastDevice = nullptr;
    };
}

int main()
{
    WXL_Api api{};
    api.structSize   = sizeof api;
    api.apiVersion   = WXL_API_VERSION;
    api.Log          = &FakeLog;
    api.Subscribe    = &FakeSubscribe;
    api.Emit         = &FakeEmit;
    api.GetInterface = &FakeGetInterface;

    // Added before Bind: waits, then is subscribed by Bind.
    Both* early = new Both();
    wxl::ScriptMgr::Add(early);
    CHECK(g_subCount == 0);
    CHECK(wxl::ScriptMgr::Bind(&api, "check"));
    const uint32_t worldHooks = 7, renderHooks = 10;
    CHECK(g_subCount == worldHooks + renderHooks);

    // Added after Bind: subscribed at once.
    Both* late = new Both();
    wxl::ScriptMgr::Add(late);
    CHECK(g_subCount == 2 * (worldHooks + renderHooks));

    wxl::events::UpdateArgs u{ 0.25f, 1234 };
    FakeEmit(uint32_t(wxl::events::Event::OnUpdate), &u);
    CHECK(early->updates == 1 && late->updates == 1);
    CHECK(early->lastDt == 0.25f && late->lastTime == 1234);

    bool handled = false;
    wxl::events::InputArgs in{ 7, 0, 0, &handled };
    FakeEmit(uint32_t(wxl::events::Event::OnInput), &in);
    CHECK(handled);

    int device = 0;
    wxl::events::EndSceneArgs es{ &device };
    FakeEmit(uint32_t(wxl::events::Event::OnEndScene), &es);
    CHECK(early->lastDevice == &device);

    // A hook of a type the script does not derive never reaches it.
    wxl::events::DoodadSpawnArgs d{ nullptr };
    FakeEmit(uint32_t(wxl::events::Event::OnDoodadSpawn), &d);
    CHECK(early->updates == 1);

    wxl::ScriptMgr::Log(WXL_LOG_INFO, "x=%d", 42);
    CHECK(std::strcmp(g_lastLog, "check: x=42") == 0);
    CHECK(std::strcmp(early->GetName(), "both") == 0);
    CHECK(wxl::ScriptMgr::Api() == &api);

    // The handles compile and keep their null semantics without a client behind them.
    wxl::game::Object none;
    CHECK(!none && none.Guid() == 0 && none.TypeMask() == 0 && !none.IsUnit());
    CHECK(!none.AsUnit() && !none.AsPlayer());
    wxl::game::Unit noUnit;
    CHECK(noUnit.Reaction(noUnit) == 0 && !noUnit.Model());

    std::printf("%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
