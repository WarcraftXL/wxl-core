# WarcraftXL

**A modding framework for the World of Warcraft 3.3.5a (build 12340) client.**

WarcraftXL loads into the running client and gives mods a clean, typed way to talk to the engine -
the same idea as RED4ext for Cyberpunk 2077 or SKSE for Skyrim. The framework owns the hard,
repetitive parts (getting into the process, the hook engine, client offsets, engine bindings, an
event bus, file-format contracts); your mods - here called **modules / scripts** - own the actual features.

> **Core principle.** If something is needed everywhere and always works the same way, it belongs in
> the core. Anything that is a *feature* - a decision, an effect, an editor - is a module. The core
> stays small and reusable; the modules stay free to do whatever they want.

## How it works

`WarcraftXL.dll` is the framework. It boots inside the client, brings up the hook engine, and raises
a set of events. Each module is a small self-contained unit under `scripts/` that subscribes to those
events and uses the core's bindings to read and drive the game. Drop a module in, rebuild, and it is
live - no separate injector, no patched data files.

The core is organised as four pillars, so a module never touches a raw address itself:

| Pillar | Namespace | What it gives a module |
|---|---|---|
| **Offsets** | `wxl::offsets` | The curated client addresses and struct layouts. Internal - modules never include these directly. |
| **Bindings** | `wxl::game` | Typed, zero-overhead calls into engine functions (`Native<Fn>(addr)(args...)`) plus an enumerable catalog of `{name, address, signature}`. |
| **Events** | `wxl::events` | A POD-dispatch event bus. A module subclasses `EventScript` and binds member functions with `on<&Self::OnEndScene>(Event::OnEndScene)`. |
| **Assets** | `wxl::asset` | In-memory contracts for the client's file formats (ADT, WMO, M2, WDT, WDL) so modules read structured data, not byte soup. |

A module looks like this - bind in the constructor, react in the handler:

```cpp
class MyModule final : public wxl::events::EventScript {
public:
    MyModule() { on<&MyModule::OnEndScene>(wxl::events::Event::OnEndScene); }
    void OnEndScene(const wxl::events::EndSceneArgs& a) { /* draw, read world, edit... */ }
};
MyModule g_myModule; // file-scope instance self-registers at load
```

## Layout

```
src/
├── core/       Hook · Logger · Mem · Main     process bring-up, hook engine, entry point
├── offsets/    engine/ · game/                client addresses + struct layouts (internal)
├── game/       camera · doodad · world · ui   typed engine bindings (the wxl::game pillar)
│               m2 · wmo · adt · unit · gx · 
│               io · mem ...
├── events/     Event · EventScript            the event bus + the module base class
├── asset/      adt · wmo · m2 · wdt · wdl     file-format contracts
├── services/   asset                          higher-level services over the pillars
└── runtime/    RenderHooks                    per-frame / device hooks the events ride on

scripts/              the modules (each builds into WarcraftXL.dll)
├── wxl-mini-noggit   an in-client map editor (ImGui + 3D gizmo): pick a doodad, move/rotate/scale it
├── wxl-unit-outline  a unit outline / highlight effect
└── wxl-glue-unlock   glue-screen unlock

deps/           vendored: MinHook, Dear ImGui + ImGuizmo, StormLib, FlatBuffers
```

Every address the bindings rely on lives in `src/offsets/`, named and annotated. The reasoning behind
each one is kept in the project's RE documentation - the code follows it.

## Building

The target client is a 32-bit process, so everything builds **Win32**.

**Requirements**
- CMake ≥ 3.25
- A Win32 C++17 toolchain (Visual Studio 2022 recommended)
- A legally-obtained 3.3.5a (12340) client

```sh
cmake -B build
cmake --build build --config Release --target WarcraftXL
```

Output: `WarcraftXL.dll`. Vendored dependencies build with the project.

## Install

1. Place `WarcraftXL.dll` next to `Wow.exe` and load it into the client (import-table entry / loader).
2. Launch. The framework writes a startup log on bootstrap - check it to confirm modules came up.

Each client process launches its own `WarcraftXLHost.exe` session (IPC named objects are scoped by
the Wow.exe PID), so multiple concurrent clients do not share a mailbox.

> Modifying a client binary is on you: work on a **copy**, keep an untouched backup, and only point
> this at a client and server you are permitted to modify and connect to.

## Contributors

Thanks to everyone who has helped shape WarcraftXL, with code, reverse-engineering, ideas, or feedback:

- [Furioz](https://github.com/Furioz420)
- [Tester](https://github.com/TesterWoWDev)
- [Duskhaven](https://git.duskhaven.net/Duskhaven)

## Support

**WarcraftXL is free, and it always will be - forever.** Nothing here is gated, and nothing ever
will be. Sponsoring is completely optional - just a way to support the project and the time behind
it, if you want to and can.

<p align="center">
  <a href="https://github.com/sponsors/iThorgrim"><img src="https://raw.githubusercontent.com/iThorgrim/ithorgrim/refs/heads/main/assets/sponsor.svg" alt="Sponsor iThorgrim" height="48"></a>
</p>

## Legal

WarcraftXL is an **interoperability project**. It distributes no Blizzard code and no game assets, and
runs only against a client you supply and own, reading that client's own files at runtime.
Reverse-engineering is limited to what is necessary for interoperability.

World of Warcraft and Wrath of the Lich King are trademarks of Blizzard Entertainment. This project is
not affiliated with or endorsed by Blizzard.

## License

Released under the **GNU General Public License v3.0** - see [LICENSE](LICENSE).

Bundles [MinHook](https://github.com/TsudaKageyu/minhook) (© Tsuda Kageyu, BSD 2-Clause),
[Dear ImGui](https://github.com/ocornut/imgui) + [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo),
and [StormLib](https://github.com/ladislav-zezula/StormLib) under `deps/`, with their licenses retained.
