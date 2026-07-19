-- hello.lua — minimal WarcraftXL extension: proves the VM loads and events reach Lua.
-- Copy into "<Wow.exe dir>/extensions/" (or the WXL_EXTENSIONS_DIR folder) and start the client.

-- Runs once, when the loader executes this chunk at VM start / hot-reload.
wxl.log("hello.lua loaded, wxl v" .. wxl.version)

-- Read a knob (env or WarcraftXL.cfg); demonstrates the config bridge with a fallback.
wxl.log_debug("dev mode = " .. wxl.config("WXL_DEV_MODE", "off"))

-- Log only the FIRST world enter so the message does not repeat on later zonings.
local greeted = false
wxl.on("world_enter", function(mapId)
  if greeted then return end
  greeted = true
  wxl.log("entered world, map " .. tostring(mapId))
end)
