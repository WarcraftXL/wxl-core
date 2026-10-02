// Lua/FrameScript landmarks for the target client (335).
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

    // lua_toboolean(state, index). Lua truth, not a type test: 0 only for nil and for the boolean
    // false, 1 for everything else including 0 and the empty string -- 0x0084E0B0 is
    // 55 8B EC 8B 45 0C 8B 4D 08 / call index2adr / 8B 48 08 (the value's type tag) / 85 C9 74 11
    // (tag 0 is nil) / 83 F9 01 75 05 83 38 00 74 07 (tag 1 is boolean, its payload 0 is false).
    // An index past the top reaches index2adr's nil object and reads as false, which is what lets a
    // binding treat an omitted argument and an explicit false alike.
    constexpr uintptr_t kLuaToBoolean = 0x0084E0B0;
    using LuaToBooleanFn = int(__cdecl*)(void* state, int index);

    // lua_touserdata(state, index). Returns the pointer a light userdata holds, the data block of a
    // full userdata, or null for anything else -- 0x0084E1C0 reads the value's type tag at +8 and
    // branches: tag 2 (light) returns *value, tag 7 (full) returns value + 0x18, otherwise xor eax.
    // A frame's Lua table holds its FrameScript_Object at the numeric key 0 as a light userdata
    // (FrameScript_Object::RegisterScriptObject, 0x00819880), so this is the step from the table
    // Lua sees back to the C++ object -- the one FrameScript_GetObjectThis (0x004A81B0) takes.
    constexpr uintptr_t kLuaToUserData = 0x0084E1C0;
    using LuaToUserDataFn = void*(__cdecl*)(void* state, int index);

    constexpr uintptr_t kLuaPushString = 0x0084E350;
    using LuaPushStringFn = void(__cdecl*)(void* state, const char* value);

    // lua_pushlstring(state, bytes, length). __cdecl, [ebp+8] state, [ebp+0xc] the bytes, [ebp+0x10]
    // the length, closing on a bare ret at 0x0084E349. lua_pushstring above is a strlen followed by a
    // call to this one, which is why a substring is pushed here rather than copied out first.
    constexpr uintptr_t kLuaPushLString = 0x0084E300;
    using LuaPushLStringFn = void(__cdecl*)(void* state, const char* bytes, size_t length);

    constexpr uintptr_t kLuaPushNil = 0x0084E280;
    using LuaPushNilFn = void(__cdecl*)(void* state);

    constexpr uintptr_t kLuaPushBoolean = 0x0084E4D0;
    using LuaPushBooleanFn = void(__cdecl*)(void* state, int value);

    // luaL_error(state, format, ...). __cdecl and variadic, caller-cleaned -- CSimpleFrame_SetScale
    // reaches it at 0x0049F8CB with 50 (the object name) / 68 <format> / 56 (the state) / E8 /
    // 83 C4 0C. IT DOES NOT RETURN: it raises, and the raise unwinds through longjmp, so a caller
    // holds nothing across it that has a destructor to run. Every stock binding reports a bad
    // argument this way, which is what makes an added binding that does the same indistinguishable
    // from one the client wrote.
    constexpr uintptr_t kLuaLError = 0x0084F280;
    using LuaLErrorFn = void(__cdecl*)(void* state, const char* format, ...);

    // FrameScript_Execute(source, chunkName, taintSource). THE STATE IS NOT AN ARGUMENT: 0x00819210
    // reads s_context (0x00D3F78C) itself, and its three stack arguments are the chunk text at
    // [ebp+8], the name luaL_loadbuffer is given for it at [ebp+0xc], and the value stored into
    // lua_tainted (0x00D4139C) for the length of the call at [ebp+0x10]. FrameScript_Initialize's own
    // call at 0x00819d26 passes ("compat.lua", 0); FrameXML_ProcessFile's at 0x008141e2 passes the
    // file's name and the taint source it inherited. A null taint source is what marks a chunk as the
    // client's own rather than an addon's.
    //
    // Stack-neutral and protected: it pushes its error handler, loads, pcalls, and settops back to
    // the top it was entered with on every path, so a chunk that fails to compile or raises is
    // reported through the client's script error path and nothing reaches the caller.
    constexpr uintptr_t kFrameScriptExecute = 0x00819210;
    using FrameScriptExecuteFn = void(__cdecl*)(const char* source, const char* chunkName,
                                                void* taintSource);

    // --- stack and table primitives ---
    // The pseudo-indices, as every call site in the binary spells them: LUA_REGISTRYINDEX is the
    // literal 0xFFFFD8F0 and LUA_GLOBALSINDEX the literal 0xFFFFD8EE, both visible in
    // FrameScript_Object::RegisterScriptObject (0x00819880) -- -10000 and -10002, Lua 5.1's values.
    constexpr int kRegistryIndex = -10000;
    constexpr int kGlobalsIndex  = -10002;

    // lua_type results. Only the three this codebase distinguishes are named. LUA_TFUNCTION is 6,
    // as the table tag above is 5: both are Lua 5.1's own order, unchanged in this build.
    constexpr int kTypeNil      = 0;
    constexpr int kTypeTable    = 5;
    constexpr int kTypeFunction = 6;

    constexpr uintptr_t kLuaSetTop = 0x0084DBF0;
    using LuaSetTopFn = void(__cdecl*)(void* state, int index);

    constexpr uintptr_t kLuaType = 0x0084DEB0;
    using LuaTypeFn = int(__cdecl*)(void* state, int index);

    constexpr uintptr_t kLuaPushValue = 0x0084DE50;
    using LuaPushValueFn = void(__cdecl*)(void* state, int index);

    // lua_replace(state, index): pops the top value and stores it at `index`. __cdecl, [ebp+8] state
    // and [ebp+0xc] index -- 0x0084DD70 opens 55 8B EC 56 8B 75 08 57 8B 7D 0C and ends
    // 83 46 0C F0 5F 5E 5D C3, the add being the one slot it pops.
    constexpr uintptr_t kLuaReplace = 0x0084DD70;
    using LuaReplaceFn = void(__cdecl*)(void* state, int index);

    // Pops the key, pushes the value, no metatable.
    constexpr uintptr_t kLuaRawGet = 0x0084E600;
    using LuaRawGetFn = void(__cdecl*)(void* state, int tableIndex);

    // Pushes t[n], no metatable. How a registry reference is turned back into its value --
    // CScriptRegion::LoadXML reaches a frame's Lua table this way, at 0x004886DA, having pushed the
    // object's ref and the literal 0xFFFFD8F0.
    constexpr uintptr_t kLuaRawGetI = 0x0084E670;
    using LuaRawGetIFn = void(__cdecl*)(void* state, int tableIndex, int n);

    // Pops value then key, no metatable.
    constexpr uintptr_t kLuaRawSet = 0x0084E970;
    using LuaRawSetFn = void(__cdecl*)(void* state, int tableIndex);

    // Pops the key, pushes key and value, or nothing and returns 0 at the end of the table.
    constexpr uintptr_t kLuaNext = 0x0084EF50;
    using LuaNextFn = int(__cdecl*)(void* state, int tableIndex);

    // --- calling, references, and compiling a chunk ---
    // lua_pcall(state, argCount, resultCount, errorHandler). Returns 0 having left resultCount
    // values, or non-zero having left the error object. FrameScript_CompileFunction reaches it
    // twice -- at 0x00819183 with (state, 0, 1, -2) to run the chunk it just loaded, and at
    // 0x0081915A with (state, 1, 0, 0) to report a load failure.
    constexpr uintptr_t kLuaPCall = 0x0084EC50;
    using LuaPCallFn = int(__cdecl*)(void* state, int argCount, int resultCount, int errorHandler);

    // luaL_ref(state, tableIndex): pops the value at the top, stores it in the table, and returns
    // the integer key it stored it under -- or -1 when the value was nil. This is how a script
    // handler is kept alive: CSimpleFrame::LoadXML_Scripts refs the compiled function into the
    // registry at 0x00490072 and keeps the key in the frame's slot.
    constexpr uintptr_t kLuaLRef = 0x0084F6C0;
    using LuaLRefFn = int(__cdecl*)(void* state, int tableIndex);

    // luaL_unref(state, tableIndex, ref): releases a key luaL_ref handed out. LoadXML_Scripts
    // releases the key a slot already held before installing the new one, at 0x0048FF82 --
    // 8B 4D FC / 50 / 68 F0 D8 FF FF / 51 / E8 -> 0x0084F7A0 / 83 C4 0C, so __cdecl and the
    // registry index is the literal 0xFFFFD8F0.
    constexpr uintptr_t kLuaLUnref = 0x0084F7A0;
    using LuaLUnrefFn = void(__cdecl*)(void* state, int tableIndex, int ref);

    // FrameScript_CompileFunction(chunkName, format, body, status): formats `format` with `body`
    // as its single %s, loads the result as a chunk named `chunkName`, runs it, and returns a
    // registry key for the one value the chunk returned -- or -1 when loading or running failed,
    // the message having been reported through `status`. 0x008190C0 in order: SStrPrintf into a
    // stack buffer sized strlen(format) + 1 + strlen(body), luaL_loadbuffer(state, buffer, length,
    // chunkName), lua_pcall(state, 0, 1, -2), luaL_ref(state, LUA_REGISTRYINDEX). Argument order
    // read at the call site 0x0048FFE1: 8B 4D 0C (status) / 8B 55 F8 (body) / 8B 45 F0 (format) /
    // 51 / 52 / 50 / lea ecx,[ebp-0x410] (buffer) / 51 / call, caller-cleaned.
    //
    // Every inline handler body in the client becomes a function through this call, and `format`
    // is what names the handler's parameters -- "return function(self,button,down) %s end" and
    // the like. A caller outside the client may pass a format of its own: the only requirement is
    // one %s and a chunk that returns exactly one value.
    constexpr uintptr_t kFrameScriptCompileFunction = 0x008190C0;
    using FrameScriptCompileFunctionFn = int(__cdecl*)(const char* chunkName, const char* format,
                                                       const char* body, void* status);

    // --- the point at which a context exists and the interface has not loaded on it ---------------
    // FrameXML_CreateFrames(tocPath, addOnName, md5Context, status): loads a .toc and every XML and
    // Lua file it names. __cdecl -- four arguments at [ebp+8] through [ebp+0x14], read at the
    // prologue (8B 5D 14 is the status), and the function closes 5F 5E B8 01 00 00 00 5B 8B E5 5D C3,
    // a bare ret with 1 in eax.
    //
    // Three callers. CGGameUI::Initialize reaches it at 0x0052ac77 for Interface\FrameXML\FrameXML.toc
    // and CGlueMgr::Resume at 0x004da84b for Interface\GlueXML\GlueXML.toc; LoadAddOn reaches it at
    // 0x005f8218 for each addon, on the context the first two built.
    //
    // Those first two are the only points in the client at which a script context has been built and
    // no interface file has run on it yet. FrameScript_Initialize (0x00819BB0) is the sole caller of
    // lua_newstate (0x00855370), so every context in the process comes from it, and both paths reach
    // it before this call -- CGGameUI::Initialize directly at 0x0052ab11, CGlueMgr::Resume through
    // FrameScript_Flush at 0x0081ac84 -- with the client's own script functions and metatables
    // registered in between (LoadScriptFunctions 0x005120E0 in one, the RegisterScriptFunctions run
    // at 0x004da79x in the other). Anything Lua has to see during an OnLoad handler has to be in
    // place before this returns control to the loader.
    constexpr uintptr_t kFrameXMLCreateFrames = 0x00814340;
    using FrameXMLCreateFramesFn = int(__cdecl*)(const char* tocPath, const char* addOnName,
                                                 void* md5Context, void* status);

    // Verifies that an indirect callback lies in Wow.exe's .text section before Lua invokes it.
    constexpr uintptr_t kValidateFunctionPointer = 0x0086B5A0;
    using ValidateFunctionPointerFn = void(__cdecl*)(uintptr_t function);

    // --- script methods on frame objects ---
    // A method as the tables hold it: the name Lua calls, then the function.
    struct ScriptMethod { const char* name; LuaCFunction function; };

    // Adds an array of methods to a metatable under construction. Every frame class registers through a
    // callback that ends in a call to this, which is what makes such a callback the place to add more:
    // the stock methods land first, ours after, on that class alone.
    constexpr uintptr_t kFillScriptMethodTable = 0x008167E0;
    using FillScriptMethodTableFn = void(__cdecl*)(void* target, const ScriptMethod* methods, int count);

    // The object a script method was invoked on, for a class's type id.
    constexpr uintptr_t kGetObjectThis = 0x004A81B0;
    using GetObjectThisFn = void*(__cdecl*)(int typeId);

    // CSimpleTexture's script metatable, held as the LUA_REGISTRYINDEX reference luaL_ref handed out
    // for it. CSimpleTexture::CreateScriptMetaTable (0x0048CD00) stores what
    // FrameScript_Object::CreateScriptMetaTable (0x00816790) returned -- that function builds
    // { __index = <method table> } and ends in luaL_ref(state, LUA_REGISTRYINDEX), so the slot holds
    // a reference key, not a pointer. CSimpleTexture::GetScriptMetaTable (0x00482D00) is the two
    // instructions A1 10 11 AC 00 C3 that read it, and every texture object is given the metatable
    // through lua_rawgeti(state, LUA_REGISTRYINDEX, <this>) in FrameScript_Object::RegisterScriptObject
    // at 0x008198CC.
    //
    // CSimpleTexture::DestroyScriptMetaTable (0x0048BC40) unrefs and writes -2 back, so a value of
    // zero or below is a slot with no live metatable -- which is what it holds between a context
    // being torn down and RegisterSimpleFrameScriptMethods (0x0081B870) rebuilding it.
    //
    // This is the one way to reach the texture method table without an object to read it off, which
    // matters because the only way to make a texture is through a frame and the first frame does not
    // exist until the interface loads.
    constexpr uintptr_t kSimpleTextureMetaTableRef = 0x00AC1110;

    // Type ids are handed out lazily from this counter: a class's slot stays zero until one of its
    // script methods runs and claims the next id. A method added to a class has to claim it the same
    // way and into the same slot -- claiming its own would resolve a different object, or none.
    constexpr uintptr_t kObjectTypeCounter = 0x00D3F778;
}
