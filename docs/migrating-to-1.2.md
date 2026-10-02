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
