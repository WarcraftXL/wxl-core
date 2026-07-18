# LuaJIT — vendored source (WarcraftXL)

LuaJIT is embedded **statically** into `WarcraftXL.dll`. No `luajit.dll`, `luajit.exe`,
or `lua51.dll` is ever shipped to a player: the client links `lua51.lib` (the static
archive) directly. The client (WoW 3.3.5a build 12340) is a 32-bit x86, single-thread
process, so the build target is **MSVC / x86 / static CRT (/MT)**.

## Source of this drop

| Field | Value |
|---|---|
| Upstream | https://github.com/LuaJIT/LuaJIT |
| Branch | `v2.1` (rolling release) |
| Archive URL | https://github.com/LuaJIT/LuaJIT/archive/refs/heads/v2.1.tar.gz |
| Downloaded | 2026-07-18 |
| Archive size | 1 093 508 bytes (~1.04 MB) |
| `.relver` | `1783773675` (commit timestamp; the rolling version stamp) |
| Version string | `LuaJIT 2.1.ROLLING` (`luajit_rolling.h`) |
| License | **MIT** (see `COPYRIGHT`) — Copyright (C) 2005-2026 Mike Pall |

The repository content is placed at the root of `deps/luajit/` (`src/`, `dynasm/`, `etc/`,
`doc/`, `COPYRIGHT`, `Makefile`, …). The upstream `.git` history is **not** vendored
(archive download, not a clone).

### `.out` bytecode note

The rolling release derives its version from the commit timestamp, so `.out` bytecode
produced by `luajit -b` is locked to this exact drop. The security manifest (added later)
must record the LuaJIT version and reject a `.out` compiled by a different build.

## License

LuaJIT is MIT-licensed; the `COPYRIGHT` file is kept **verbatim**. Vendored LuaJIT sources
carry their original MIT headers and **must not** receive the project's GPLv3 header. Only
files authored by WarcraftXL (the C++ under `src/engine/lua`) carry GPLv3.

## Building a static x86 /MT archive with MSVC

The build is not a plain `cl *.c`: LuaJIT bootstraps a code generator (`minilua` →
`buildvm` via DynASM) that emits `lj_vm.obj` and several generated headers, then compiles
the core. The supported entry point is `src/msvcbuild.bat`.

### Steps (manual, for reference)

1. Open a **32-bit** Visual Studio command prompt (`vcvarsall.bat x86`, or the
   "x86 Native Tools Command Prompt"). `INCLUDE`/`LIB` must point at the x86 toolchain.
2. `cd deps\luajit\src`
3. Run: `msvcbuild.bat static`
4. Output: `lua51.lib` (static archive) in `deps\luajit\src`. Headers to include:
   `lua.h`, `lauxlib.h`, `lualib.h` (and the generated `luajit.h`, see below).

`msvcbuild.bat static` takes the `:STATIC` path, which compiles with `%LJCOMPILE%`
**without** the `%LJDYNBUILD%` fragment (that fragment is what carries `/MD`). So the static
path does not add `/MD`. However the CRT model must be made **explicit** `/MT` — do not rely
on the compiler's default — to match the project-wide static CRT.

### Forcing /MT WITHOUT editing the vendored script

The vendored `src/msvcbuild.bat` must stay pristine. `msvcbuild.bat` does `@setlocal` and
re-assigns `LJCOMPILE` on its line 20, so an environment override before the call is
discarded. The clean approach is to build from a **copy** of the script (the CMake
integration does exactly this — see `docs/vm-build-notes.md`) with a one-line edit:

Original line 20 of `src/msvcbuild.bat`:

```
@set LJCOMPILE=cl /nologo /c /O2 /W3 /D_CRT_SECURE_NO_DEPRECATE /D_CRT_STDIO_INLINE=__declspec(dllexport)__inline
```

Edited (insert `/MT`):

```
@set LJCOMPILE=cl /nologo /c /O2 /W3 /MT /D_CRT_SECURE_NO_DEPRECATE /D_CRT_STDIO_INLINE=__declspec(dllexport)__inline
```

That is the only change required for the static /MT archive. Notes:

- `/D_CRT_STDIO_INLINE=__declspec(dllexport)__inline` on line 20 marks the CRT stdio inline
  helpers for DLL export. It is harmless in a static archive linked into `WarcraftXL.dll`
  (nothing forces those symbols to be re-exported from our DLL), so it is left as-is.
- For a **debug** archive add the `debug` argument (`msvcbuild.bat static debug`); the debug
  runtime is `/MTd`, so apply the same edit as `/MTd` if a debug static CRT is wanted.
- `nogc64` is x64-only (GC64) and irrelevant on x86; do not pass it.

### Generated `luajit.h`

In the rolling release, `src/luajit.h` does **not** exist in the source tree — it is
generated during the build (`minilua host/genversion.lua`, from `luajit_rolling.h`). The
MVP C++ (`src/engine/lua`) therefore includes only `lua.h`, `lauxlib.h`, `lualib.h`, which are
present in the vendored tree. When `luaJIT_*` APIs (e.g.
`luaJIT_setmode`, the profiler) are needed, add `luajit.h` to the include shim and point the
compile at the **built** tree (which contains the generated `luajit.h`), not the pristine
source tree.
