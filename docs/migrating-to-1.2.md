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

## Typed reads, object handles

`wxl/game/Binding.hpp` now holds, next to `Native<Fn>`: `Read<T>(address)`, `Write<T>(address, v)`,
`At<T>(base, offset)` and `Virtual<Fn>(object, slot)`. The bindings use them in place of raw casts;
an extension that reads client memory can do the same. The two private vtable helpers
(`gx::Vtbl`, `world::detail::Virtual`) are gone: call `wxl::game::Virtual<Fn>(obj, slot)`.

`world::MapId()` is deprecated: call `world::CurrentMapId()`. `Loading.hpp` includes `World.hpp`,
and its duplicate `EnterMap` is gone (the one in `World.hpp` is the same function).

`wxl/objects/Object.hpp`, `Unit.hpp` and `Player.hpp` add the handles `wxl::Object`, `wxl::Unit` and
`wxl::Player`, one type per file: one pointer, no ownership, methods over the same bindings
(`Player::Active().Position()`, `unit.IsHostileTo(other)`, `Object::FromGuid(guid)`). Include the
most derived type you need; `Player.hpp` pulls in `Unit.hpp` and `Object.hpp`. The free functions
stay.

## Hooks, services and the config reader

Three pieces of `WXL_Api` plumbing every extension was writing for itself now ship with the SDK. The
raw table is untouched, so none of this is forced: `ScriptMgr::Api()` keeps working.

**`wxl/Hook.hpp`** — `wxl::Hook<Fn>` holds a detour and the chain link behind it under one function
type, so a detour wired to the wrong trampoline no longer compiles. `Fn` may be written out as a
function type (`void __cdecl(void*)`) or named through one of the `offsets/` aliases, which are
function *pointer* types; `Hook` strips the pointer, so both spellings land on the same type.

```cpp
// before: a global for the original, a cast per attach
adt::Map_ChunkBuildFn g_origChunkBuild = nullptr;
void __fastcall hkChunkBuild(void* c, void* edx, void* raw, int flag)
{
    g_origChunkBuild(c, edx, raw, flag);
}
g_api->HookAttachByName("Adt.ChunkBuild", reinterpret_cast<void*>(&hkChunkBuild),
                        reinterpret_cast<void**>(&g_origChunkBuild), 0);
// after
wxl::Hook<adt::Map_ChunkBuildFn> g_chunkBuild;
void __fastcall hkChunkBuild(void* c, void* edx, void* raw, int flag)
{
    g_chunkBuild(c, edx, raw, flag);
}
g_chunkBuild.Attach("Adt.ChunkBuild", &hkChunkBuild);
```

Prefer the alias over a retyped signature: it keeps one declaration of the prototype, so a change to
the engine signature fails to compile at the detour instead of passing the wrong arguments.

`Attach` also has an address overload for a point the core does not name. Calling the handle calls
the next link in the chain; `Original()` hands it over if you would rather be explicit.

A `Hook` at namespace scope is constant-initialised, so it needs no load-order care. It models a
hook *point*, though: a vtable slot that must be re-patched per device object stays a raw pointer
plus `wxl::mem::SwapPointer`.

**`wxl/Service.hpp`** — `wxl::Service<T>` resolves a published capability on first use and keeps it,
which is the lazy accessor each extension had copied. A lookup that finds nothing is retried, so a
service published after the first attempt is still picked up. `wxl::Publish<T>` is the typed
counterpart of `ScriptMgr::GetInterface<T>`, and does the `const_cast` the raw call needs once.

```cpp
static wxl::Service<WXL_FdidApi> g_fdid("wxl.fdid", WXL_FDID_API_VERSION);
if (g_fdid) g_fdid->ResolveTexture(path, out, cap);

wxl::Publish("wxl.db2", WXL_DB2_API_VERSION, &g_db2Api);
```

**`wxl/Config.hpp`** — the per-extension env-var + `.cfg` reader moved out of the core's private
`src/common/`, where extensions were reaching into it. The API and the `wxl::ext::config` namespace
are unchanged, and `wxl::config` is now an alias for it. `common/CfgParse.hpp` moved to
`wxl/CfgParse.hpp` the same way.

- Replace `#include "common/ExtensionConfig.hpp"` with `#include "wxl/Config.hpp"`.
- Both old paths still compile through forwarding headers that print the new path.

## More object handles (nothing to do)

`wxl/objects/` gains seven handles beside `Object`, `Unit` and `Player`. Each one wraps a raw pointer
the bindings already hand out, so the free functions they call stay exactly as they are: an extension
that passes `void*` around keeps working, and these are the shorter way to write the same thing.

| Handle | Wraps | Where one comes from |
|---|---|---|
| `wxl::GameObject` | a game object | `Object::AsGameObject()`, `GameObject::FromGuid` |
| `wxl::Doodad` | a placed map doodad | `MapChunk::ForEachDoodad` |
| `wxl::MapChunk` | a terrain chunk | `MapChunk::At(pos)` |
| `wxl::MapTile` | a resident map tile | `MapTile::Slot(second, first)` |
| `wxl::Camera` | the engine's active camera | `Camera::Active()` |
| `wxl::Model` | an M2 model object | the `ModelScript` hooks' `model` argument |
| `wxl::Wmo`, `wxl::WmoGroup` | a map object's root and groups | the `AssetScript` WMO hooks |

```cpp
// before
void* d = /* a chunk entry */;
if (wxl::game::doodad::IsValid(d)) {
    float p[3]; wxl::game::doodad::Position(d, p);
    char name[128]; wxl::game::doodad::ModelName(d, name, sizeof name);
}
// after
if (wxl::Doodad d = /* a chunk entry */) {
    const wxl::Vec3 p = d.Position();
    const char* name = d.ModelName().Text();
}
```

Two things worth knowing before reaching for one:

- `Camera`'s view, projection, view-projection and position members are `static`: that state is the
  active render state, not a property of one camera. Only the field of view belongs to the handle.
- `Unit::Model()` returns the attachment-chain node, **not** the M2 model object `wxl::Model` wraps.
  The two are different objects and handing one to the other is a bug.

## Service headers moved out of the core

`AppearanceApi.h`, `Db2Api.h`, `LightApi.h` and `ModelDataApi.h` left `include/wxl/`: they are not
core contracts, they are what `wxl-db2` publishes, so they now live in that extension's own repo,
under `src/api/`. `M2DrawApi.h` left the same way, into `wxl-modern-m2`'s `src/render/`. Nothing in
the core publishes or consumes any of the five, so this only affects an extension that included one
of them directly.

- Replace `#include "wxl/Db2Api.h"` (or `AppearanceApi.h`, `LightApi.h`, `ModelDataApi.h`) by a path
  into `wxl-db2`'s own `src/api/`, relative to your file.
- Replace `#include "wxl/M2DrawApi.h"` by a path into `wxl-modern-m2`'s `src/render/`.
- `FdidApi.h`, `StorageApi.h`, `M2ArenaApi.h` and `PluginApi.h` stay in `include/wxl/`: the first is
  consumed by more than one extension and the other three are published by the core itself, not by
  an extension, so they are genuinely core contracts.
- `InterfaceApi.h`, `JsonApi.h`, `LoadPoolApi.h`, `LodApi.h` and `ModernBlpApi.h` also stay for now:
  each names an owning extension (`wxl-interface-reforged`, `wxl-json`, `wxl-engine-reforged`,
  `wxl-modern-engine`, `wxl-modern-blp`) that is not reachable as a client extension to move the
  header into.
