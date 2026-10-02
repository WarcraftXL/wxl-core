# WarcraftXL

**A modding framework for the World of Warcraft 3.3.5a (build 12340) client.**

WarcraftXL loads into the running client and gives mods a clean, typed way to talk to the engine -
the same idea as RED4ext for Cyberpunk 2077 or SKSE for Skyrim. The framework owns the hard,
repetitive parts (getting into the process, the hook engine, client offsets, engine bindings, an
event bus); your mods - here called **extensions** - own the actual features.

> **Core principle.** If something is needed everywhere and always works the same way, it belongs in
> the core. Anything that is a *feature* - a decision, an effect, an editor - is an extension. The
> core stays small and neutral; the extensions stay free to do whatever they want.

## How it works

`WarcraftXL.dll` is the framework. It boots inside the client, brings up the hook engine, raises a
set of events, and loads every `Extensions/<Name>/<Name>.dll` found next to the client. An extension
is its own repository and its own DLL: it exports two entry points, receives the core's service
table, subscribes to events, and uses the core's bindings to read and drive the game.

The core is organised as three pillars, so an extension never touches a raw address itself:

| Pillar | Namespace | What it gives an extension |
|---|---|---|
| **Offsets** | `wxl::offsets` | The curated client addresses and struct layouts. Internal: an extension never includes these. |
| **Bindings** | `wxl::game` | Typed, zero-overhead calls into engine functions (`Native<Fn>(addr)(args...)`) and typed readers of engine objects. |
| **Scripts** | `wxl` | Script types with one virtual per hook (`WorldScript`, `RenderScript`, `ModelScript`, `ObjectScript`, `AssetScript`), their tables under `include/wxl/scripts/`, and a `ScriptMgr`. |

An extension looks like this - derive a type, override what you need, add the script:

```cpp
#include "wxl/Script.hpp"

class MyScript final : public wxl::RenderScript {
    void OnEndScene(void* device) override { /* draw, read world, edit... */ }
};

WXL_DECLARE_EXTENSION("my-extension", 1)   // writes WXL_Query and WXL_Load

void AddScripts() { wxl::ScriptMgr::Add(new MyScript()); }
```

## Layout

```
include/wxl/    what an extension includes: the C ABI (PluginApi.h, Common.h, Events.hpp), the
                C++ SDK (Common.hpp, Script.hpp and the scripts/*.def tables)
src/
├── common/     logger, configuration, page-protection helpers, shared by every binary
├── offsets/    engine/ · game/      client addresses, function types and struct layouts (internal)
├── game/       camera · doodad · world · unit · m2 · wmo · gx ...   typed engine bindings
├── engine/     hook engine, event bus, overlay, input, storage, diagnostics
├── client/     one folder per client class: the detours that publish the events
├── runtime/    DllMain, the extension loader, the service table
├── patcher/    wxl-patcher.exe
├── engine/gpu/ d3d9.dll, the render proxy
└── probe/      development checks
cmake/          build settings, helpers, the Linux toolchain
deps/           vendored: MinHook, Dear ImGui
extensions/     local clones of extensions (ignored by git; each has its own repository)
```

Every address the bindings rely on lives in `src/offsets/`, named and annotated.

## Building

The target client is a 32-bit process, so everything builds **Win32**.

**Requirements**
- CMake ≥ 3.20
- A Win32 C++20 toolchain (Visual Studio 2022 or later on Windows, clang mingw-w64 on Linux)
- A legally-obtained 3.3.5a (12340) client

```sh
cmake -B build
cmake --build build --config Release --target WarcraftXL
```

Output: `WarcraftXL.dll`. Vendored dependencies build with the project.

**From Linux.** The same Windows binaries (`.dll`, `.exe`) cross-compile with a clang-based
mingw-w64 toolchain: [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) unpacked anywhere
(`~/.local/opt/llvm-mingw` is found on its own; otherwise set `WXL_MINGW_ROOT` or put its `bin/` on
`PATH`). GCC is not enough: the client code relies on SEH and MSVC intrinsics that only clang
compiles. Then:

```sh
./build.sh --client /path/to/client      # build + deploy, like build.ps1; the path is remembered
./build.sh --target WarcraftXL           # one target
cmake -S . -B build/mingw-x86 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/mingw-i686.cmake   # by hand
```

## API reference

```sh
python3 scripts/docgen/generate.py    # writes docs/site/, open docs/site/index.html
```

Builds a local, offline reference from the doc comments already on the public headers (events,
script hooks, the object handles, the game bindings). Regenerate it after changing one of those
comments; nothing under `docs/site/` is committed.

## Install

1. Place `WarcraftXL.dll` next to `Wow.exe` and load it into the client (import-table entry / loader).
2. Launch. The framework writes a startup log on bootstrap - check it to confirm the extensions came up.

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

Bundles [MinHook](https://github.com/TsudaKageyu/minhook) (© Tsuda Kageyu, BSD 2-Clause) and
[Dear ImGui](https://github.com/ocornut/imgui) (MIT) under `deps/`, with their licenses retained.
