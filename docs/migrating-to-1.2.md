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
