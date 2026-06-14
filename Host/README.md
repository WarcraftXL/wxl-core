# WraithHost

**The engine's data backend.** A 64-bit companion process that holds the heavy data the 32-bit
client can't, and serves it on demand.

Part of [WraithEngine](https://github.com/WraithEngine). Where `Wraith.dll` lives inside the game
and does the rendering work, `WraithHost.exe` runs beside it and answers two questions the old
client can't answer on its own: *where is this asset?* and *give me its bytes.*

## Why it exists

The WotLK 3.3.5a client is a **32-bit** process - a cramped, ~limited address space. Modern WoW
data is the opposite of cramped:

- assets are addressed by numeric **FileDataID** (and materials by **MaterialResourcesID**), not by
  path - and the lookup tables that map those IDs are large;
- the archives themselves are big, and you don't want whole modern files sitting in the client's
  tight address space.

So WraithHost runs as a **separate 64-bit process** with no 32-bit memory ceiling. It carries the
ID tables and owns the file-read path, and the client simply asks it for what it needs. Think of it
as the engine's librarian and warehouse: the client knows *what* it wants, the host knows *where* it
is and *hands over* the bytes.

## What it does

**1. Resolves modern IDs to paths.**
It loads the modern lookup tables (`ModelFilePath`, `TextureFilePath`, `TextureFileData`) and
answers resolution requests:

- model FileDataID → path
- texture FileDataID → path
- MaterialResourcesID (+ a type hint) → FileDataID → path - the chain a modern model needs to find
  the actual texture behind a material

**2. Serves files from the client's archives.**
It mounts the client's MPQ archive set with StormLib, in the same priority order the native client
uses, plus the loose `Patch-N.MPQ\` override folders. Then it serves file bytes with a
memory-conscious strategy:

- **small files** are read whole and returned inline - one round trip, no handle kept;
- **large files** are kept open and read **by range on demand**, so the host never holds a whole
  big file in RAM and the client pulls it chunk by chunk.

## How it connects

The client (`Wraith.dll`, 32-bit) and the host (`WraithHost.exe`, 64-bit) talk over a
**shared-memory mailbox**: a 1 MiB shared window plus two events forming a single-slot
request/response channel. The payload is a FlexBuffers buffer - architecture- and endian-neutral,
so the 32-bit and 64-bit sides agree on every byte. One request is in flight at a time; the client
serializes.

```
  ┌──────────────────────┐   resolve / file request    ┌───────────────────────┐
  │  Wraith.dll (32-bit) │ ──────────────────────────► │  WraithHost.exe       │
  │  inside Wow.exe      │                             │  (64-bit)             │
  │                      │ ◄────────────────────────── │  ID tables + MPQ set  │
  └──────────────────────┘     path / bytes (chunked)  └───────────────────────┘
                    shared-memory mailbox (FlexBuffers payload)
```

The host is spawned with the game's process id, runs from the client's `Utils` folder (so the
client root is its parent), watches that process, and exits when the game closes. A session-scoped
mutex keeps a single host per session.

## Build & run

64-bit target (the whole point), built from the Engine tree:

```sh
cmake -B build-host -A x64 Host
cmake --build build-host --config Release
```

It's launched by the engine's bootstrap, not by hand, but it accepts:

- `--data <dir>` - override the data directory (defaults to the host's own folder)
- `--client-pid <pid>` - the game process to shadow; the host exits when it closes

## Third-party

Reads MPQ archives via [StormLib](https://github.com/ladislav-zezula/StormLib) (© Ladislav Zezula,
MIT) and uses [FlatBuffers](https://github.com/google/flatbuffers) (© Google, Apache-2.0) for the
IPC payload, both vendored under `../vendor` with their licenses retained.

WraithHost ships no Blizzard data - it reads the archives and tables from a client you supply and
own. See the [Engine](..) README for the project's interoperability and licensing terms.