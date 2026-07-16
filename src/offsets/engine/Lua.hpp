// Lua/FrameScript landmarks for the 3.3.5a (12340) client.
// Copyright (C) 2026 WarcraftXL

#pragma once

#include <cstddef>
#include <cstdint>

namespace wxl::offsets::engine::lua
{
    using LuaCFunction = int(__cdecl*)(void* state);

    // Returns the active FrameScript Lua state, or null before script initialization.
    constexpr uintptr_t kFrameScriptGetContext = 0x00817DB0;
    using FrameScriptGetContextFn = void*(__cdecl*)();

    // Adds a global Lua function to the active FrameScript context.
    constexpr uintptr_t kFrameScriptRegisterFunction = 0x00817F90;
    using FrameScriptRegisterFunctionFn = void(__cdecl*)(const char* name, LuaCFunction function);

    // Lua 5.1 number push; WoW's lua_Number is double.
    constexpr uintptr_t kLuaPushNumber = 0x0084E2A0;
    using LuaPushNumberFn = void(__cdecl*)(void* state, double value);

    constexpr uintptr_t kLuaGetTop = 0x0084DBD0;
    using LuaGetTopFn = int(__cdecl*)(void* state);

    constexpr uintptr_t kLuaIsNumber = 0x0084DF20;
    using LuaIsNumberFn = int(__cdecl*)(void* state, int index);

    constexpr uintptr_t kLuaIsString = 0x0084DF60;
    using LuaIsStringFn = int(__cdecl*)(void* state, int index);

    constexpr uintptr_t kLuaToNumber = 0x0084E030;
    using LuaToNumberFn = double(__cdecl*)(void* state, int index);

    constexpr uintptr_t kLuaToString = 0x0084E0E0;
    using LuaToStringFn = const char*(__cdecl*)(void* state, int index, size_t* length);

    constexpr uintptr_t kLuaPushString = 0x0084E350;
    using LuaPushStringFn = void(__cdecl*)(void* state, const char* value);

    constexpr uintptr_t kLuaPushNil = 0x0084E280;
    using LuaPushNilFn = void(__cdecl*)(void* state);

    constexpr uintptr_t kLuaPushBoolean = 0x0084E4D0;
    using LuaPushBooleanFn = void(__cdecl*)(void* state, int value);

    constexpr uintptr_t kFrameScriptExecute = 0x00819210;
    using FrameScriptExecuteFn = void(__cdecl*)(const char* source, void* state);

    // Verifies that an indirect callback lies in Wow.exe's .text section before Lua invokes it.
    constexpr uintptr_t kValidateFunctionPointer = 0x0086B5A0;
    using ValidateFunctionPointerFn = void(__cdecl*)(uintptr_t function);
}
