# Migrating an extension to WarcraftXL 1.2

One section per change that touches an extension, in the order the changes landed. Each section
says what changed, why, and the steps to take. Sections marked **nothing to do** are listed so the
history is complete.

## Cleanup of the core tree (nothing to do)

Removed from the core, none of which an extension included:

- `src/config.hpp` (one compile-time switch, folded into its only user).
- `src/engine/events/EventScript.hpp`, a copy of `include/wxl/EventScript.hpp`. The public header
  under `include/wxl/` is the one to include, as before.
- `deps/stormlib` and `deps/flatbuffers`, which nothing built.

`common/ExtensionConfig.hpp` keeps its API; its parser now lives in `common/CfgParse.hpp`, which it
includes itself.

## Events: `wxl/Events.hpp` and `events/Events.def`

The event ids and their args structs moved to the public header `wxl/Events.hpp`; the ids are now
written explicitly in `include/wxl/events/Events.def`, one row per event, and never renumbered.
`engine/events/Event.hpp` still compiles for an extension but prints a message: it is the core's bus.

- Replace `#include "engine/events/Event.hpp"` with `#include "wxl/Events.hpp"`. The names
  (`wxl::events::Event::OnUpdate`, `wxl::events::UpdateArgs`) are unchanged.
- `wxl::events::Args<Event::OnUpdate>` names the args struct of an event, for templates.
- Nothing changes for `WXL_Api::Subscribe` and `WXL_Api::Emit`: the ids are the same numbers.

## Hook points: `runtime/HookPoints.def`

The named hook points are now one table, `src/runtime/HookPoints.def`, that also serves the core's
own detours: every `wxl::hook::Install("Label", address, ...)` of the core became
`hookpoints::Attach("Section.Name", ...)`, so the core and the extensions share the same chain and
the same name in the log. One duplicate row (`M2.SharedSetIndices`) was dropped; every other name is
unchanged, and `WXL_Api::HookAttachByName` works as before.

## Scripts: `wxl/Script.hpp` replaces `EventScript` and the hand-written entry points

`EventScript::on<>` and `EventScript::Bind` still work and now print a deprecation warning. The
replacement is a script type with one virtual per hook; the hooks of each type are the rows of
`include/wxl/scripts/<Type>Script.def`:

| Type | Hooks |
|---|---|
| `WorldScript` | OnUpdate, OnWorldEnter, OnWorldLeave, OnInput, OnWorldClick, OnTargetChanged, OnSoundPlay |
| `RenderScript` | OnFrame, OnEndScene, OnDeviceLost, OnDeviceReset, OnWorldRender, OnWorldRenderEnd, OnWorldSceneEnd, OnLiquidRender, OnGrassWind, OnAdtHeightBlend |
| `ModelScript` | OnModelLoadPre, OnModelLoad, OnM2SkinFinalize, OnM2PerFrameUpdate, OnBuildBonePalette, OnM2BatchDraw, OnM2SetupBatchAlpha, OnRibbonDraw, OnM2NativeLoad |
| `ObjectScript` | OnObjectUpdate, OnObjectDestroy, OnDoodadSpawn, OnItemSlotChange, OnItemSlotClear |
| `AssetScript` | OnAdtChunkBuild, OnAdtSplitTileLoad, OnWmoRootLoad, OnWmoGroupLoad, OnTextureUpload, OnBlpLoad |

Before:

```cpp
class Mod final : public wxl::ext::EventScript {
public:
    Mod() { on<&Mod::OnUpdate>(wxl::events::Event::OnUpdate); }
    void OnUpdate(const wxl::events::UpdateArgs& a) { tick(a.dt); }
};
const WXL_PluginInfo* __cdecl WXL_Query(void) { static const WXL_PluginInfo i = { sizeof i, WXL_API_VERSION, "mod", 1, WXL_CLIENT_BUILD }; return &i; }
int __cdecl WXL_Load(const WXL_Api* api) { wxl::ext::EventScript::Bind(api); static Mod mod; return 1; }
```

After:

```cpp
#include "wxl/Script.hpp"
class Mod final : public wxl::WorldScript {
    void OnUpdate(float dt, uint32_t timeMs) override { tick(dt); }
};
WXL_DECLARE_EXTENSION("mod", 1)
void AddScripts() { wxl::ScriptMgr::Add(new Mod()); }
```

Steps:

1. Replace the base class by the type (or types: a class may derive several) that holds the hooks
   you use, and give each handler the signature of its row in the `.def`. Args structs with more
   than four fields arrive as `const XxxArgs&`; a `bool*` of the struct arrives as `bool&`.
2. Delete `WXL_Query` and `WXL_Load`; write `WXL_DECLARE_EXTENSION("name", version)` and an
   `AddScripts()` that adds each script with `wxl::ScriptMgr::Add(new ...)`.
3. Replace `api->Log(level, "tag", ...)` by `wxl::ScriptMgr::Log(level, ...)` and a kept `api`
   pointer by `wxl::ScriptMgr::Api()`. `ScriptMgr::GetInterface<T>(name, version)` looks a service up.

A script receives every hook of its type; an empty default costs one virtual call per event.

## The SDK lives under `include/wxl/`

The bindings (`src/game/*.hpp`), the offsets (`src/offsets/`) and the M2 format contract moved:

| Before | After |
|---|---|
| `#include "game/Unit.hpp"` | `#include "wxl/game/Unit.hpp"` |
| `#include "offsets/game/Unit.hpp"` | `#include "wxl/offsets/game/Unit.hpp"` (the boundary rule still applies: an extension reads offsets through the bindings) |
| `#include "engine/assets/shared/models/m2/M2Format.hpp"` | `#include "wxl/formats/M2Format.hpp"` |

The old paths still compile through forwarding headers that print the new path. Namespaces are
unchanged (`wxl::game::unit`, `wxl::offsets::game::unit`, `wxl::structure::m2`). An extension that
includes nothing else from `src/` can drop `src` from its include paths.
