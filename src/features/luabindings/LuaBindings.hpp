// WarcraftXL Lua globals exposed to addons.
// Copyright (C) 2026 WarcraftXL

#pragma once

#include <cstdint>

namespace wxl::runtime::lua
{
    using Callback = int(__cdecl*)(void* state);

    /** Adds a reload-safe global Lua callback. Names and callbacks remain live for the process lifetime. */
    bool RegisterFunction(const char* name, Callback function);

    /** Adds Lua source executed once whenever WoW creates a new FrameScript state. */
    bool RegisterScript(const char* name, const char* source);

    /** Adds a custom client CVar through WoW's native RegisterCVar Lua API on each new state. */
    bool RegisterCVar(const char* name, const char* defaultValue);

    // ABI-safe wrappers exposed to external WXL modules.
    int __cdecl GetTop(void* state);
    int __cdecl IsNumber(void* state, int index);
    int __cdecl IsString(void* state, int index);
    double __cdecl ToNumber(void* state, int index);
    const char* __cdecl ToString(void* state, int index, uint32_t* length);
    void __cdecl PushNumber(void* state, double value);
    void __cdecl PushString(void* state, const char* value);
    void __cdecl PushBoolean(void* state, int value);
    void __cdecl PushNil(void* state);

    /** Executes source synchronously in the current FrameScript state. */
    bool ExecuteCurrent(const char* label, const char* source);

    /** Creates the narrow callback-validator hook before the global hook batch is enabled. */
    void InstallHooks();

    /** Marks the validator hook live after the global MinHook batch has been enabled. */
    void Activate();

    /** Registers WXL's addon-facing Lua globals when FrameScript is initialized. */
    void Install(bool force = false);
}
