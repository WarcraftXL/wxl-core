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

#include "wxl/Hook.hpp"
#include "wxl/Script.hpp"
#include "wxl/Service.hpp"
#include "wxl/objects/Camera.hpp"
#include "wxl/objects/Doodad.hpp"
#include "wxl/objects/GameObject.hpp"
#include "wxl/objects/MapChunk.hpp"
#include "wxl/objects/MapTile.hpp"
#include "wxl/objects/Model.hpp"
#include "wxl/objects/Player.hpp"
#include "wxl/objects/Wmo.hpp"

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
    // One published service, so Service<T> has something to resolve, and a hook table that answers
    // one name, so Hook can be driven without the engine behind it.
    struct Published { const char* name; uint32_t version; void* iface; };
    Published g_published{};

    void __cdecl FakePublishInterface(const char* name, uint32_t version, void* iface)
    {
        g_published = { name, version, iface };
    }
    void* __cdecl FakeGetInterface(const char* name, uint32_t version)
    {
        const bool match = g_published.name && name && std::strcmp(g_published.name, name) == 0 &&
                           g_published.version == version;
        return match ? g_published.iface : nullptr;
    }

    using ProbeFn = int __cdecl(int);
    int __cdecl ChainEnd(int v) { return v + 1; }
    const char* g_attachedName = nullptr;
    int         g_attachedPriority = -1;

    int __cdecl FakeHookAttachByName(const char* pointName, void* detour, void** original, int priority)
    {
        if (!pointName || std::strcmp(pointName, "Probe.Point") != 0) return 0;
        g_attachedName     = pointName;
        g_attachedPriority = priority;
        (void)detour;
        *original = reinterpret_cast<void*>(&ChainEnd);
        return 1;
    }

    struct FakeService { uint32_t structSize; int value; };

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
    api.Log              = &FakeLog;
    api.Subscribe        = &FakeSubscribe;
    api.Emit             = &FakeEmit;
    api.GetInterface     = &FakeGetInterface;
    api.PublishInterface = &FakePublishInterface;
    api.HookAttachByName = &FakeHookAttachByName;

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

    // The handles compile and keep their null semantics without a client behind them. Only the
    // members whose binding guards its pointer are called here: a factory (Active, At, Slot,
    // FromGuid) reads a fixed client address, and Model's and Wmo's readers dereference the object,
    // so neither can run outside the client.
    wxl::Object none;
    CHECK(!none && none.Guid() == 0 && none.TypeMask() == 0 && !none.IsUnit());
    CHECK(!none.AsUnit() && !none.AsPlayer() && !none.AsGameObject());
    wxl::Unit noUnit;
    CHECK(noUnit.Reaction(noUnit) == 0 && !noUnit.Model());
    wxl::Player noPlayer;
    CHECK(!noPlayer && !noPlayer.AsUnit() && !noPlayer.AsPlayer());
    wxl::GameObject noGameObject;
    CHECK(!noGameObject && noGameObject.Guid() == 0 && !noGameObject.IsGameObject());

    wxl::Doodad noDoodad;
    CHECK(!noDoodad && !noDoodad.Instance() && !noDoodad.ModelName());
    CHECK(noDoodad.Scale() == 1.0f && noDoodad.Position().x == 0.0f && noDoodad.Center().z == 0.0f);
    wxl::Vec3 lo{}, hi{};
    CHECK(!noDoodad.BBox(lo, hi) && !noDoodad.LocalBounds(lo, hi));
    char nameBuf[8] = { 'x' };
    CHECK(!noDoodad.ModelName(nameBuf, sizeof nameBuf));

    wxl::MapChunk noChunk;
    CHECK(!noChunk && noChunk.NearObjectCount(0) == 0);
    int visited = 0;
    CHECK(noChunk.ForEachDoodad([&](wxl::Doodad) { ++visited; }) == 0 && visited == 0);

    wxl::MapTile noTile;
    CHECK(!noTile && noTile.TileFirst() < 0 && noTile.TileSecond() < 0);

    // A null camera: the field-of-view binding answers with its own fallback and the setter no-ops.
    wxl::Camera noCamera;
    CHECK(!noCamera && noCamera.Fov() > 0.0f);
    noCamera.SetFov(1.0f);

    wxl::Model noModel;
    wxl::Wmo noWmo;
    wxl::WmoGroup noWmoGroup;
    CHECK(!noModel && !noWmo && !noWmoGroup);

    // Hook: an unknown point fails and leaves the chain link null; a known one fills it, and calling
    // the hook passes through to what the core handed back.
    wxl::Hook<ProbeFn> hook;
    CHECK(!hook && !hook.Original());
    CHECK(!hook.Attach("Probe.Unknown", &ChainEnd));
    CHECK(!hook);
    CHECK(hook.Attach("Probe.Point", &ChainEnd, 3));
    CHECK(hook && hook.Original() == &ChainEnd);
    CHECK(std::strcmp(g_attachedName, "Probe.Point") == 0 && g_attachedPriority == 3);
    CHECK(hook(41) == 42);

    // Service: unresolved until something publishes the exact name and version, then cached.
    static const FakeService s_service{ sizeof(FakeService), 7 };
    wxl::Service<FakeService> wrongVersion("probe.service", 2);
    wxl::Service<FakeService> service("probe.service", 1);
    CHECK(!service && service.Get() == nullptr);
    CHECK(wxl::Publish("probe.service", 1, &s_service));
    CHECK(service && service->value == 7);
    CHECK(!wrongVersion);
    CHECK(std::strcmp(service.Name(), "probe.service") == 0 && service.Version() == 1);

    std::printf("%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
