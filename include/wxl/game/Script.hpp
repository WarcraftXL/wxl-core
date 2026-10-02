// Reading a script call's arguments and answering it.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstddef>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/engine/Lua.hpp"

/**
 * @brief The arguments a script call carries, and what it hands back.
 *
 * Indices are one-based. A method invoked as frame:Method(a, b) puts the frame at 1 and its first
 * argument at 2 -- read the frame with wxl::game::glue::MethodSelf() rather than from the stack.
 *
 * A return value is pushed, and the count of pushes returned from the function.
 */
namespace wxl::game::script
{
    namespace off = wxl::offsets::engine::lua;

    /// A script function, as the method tables and the global registry hold it.
    using Function = off::LuaCFunction;

    // The engine checks every script callback against the bounds of its own image before invoking it,
    // and a function compiled into an extension lies outside them by construction -- the call is
    // refused with a fatal "Invalid function pointer", which is what makes this seam the gate any
    // script function added from outside has to pass.
    //
    // Detour it and return, without calling the original, for the pointers you registered yourself.
    // Anything else must still reach the original: the check is doing its job for the calls it was
    // written for, and widening it wholesale would give that up for all of them.

    /// Entry the engine validates a script callback through.
    constexpr uintptr_t kValidateCallbackSeam = off::kValidateFunctionPointer;

    /// Its signature, for a detour and the matching trampoline.
    using ValidateCallbackFn = off::ValidateFunctionPointerFn;

    // The client builds a script context and loads the whole interface onto it inside one call, with
    // no frame tick in between: CGGameUI::Initialize (0x0052A980) runs FrameScript_Destroy,
    // FrameScript_Initialize, LoadScriptFunctions, then FrameXML_CreateFrames, and CGlueMgr::Resume
    // (0x004DA5F0) runs FrameScript_Flush, the glue RegisterScriptFunctions, then the same
    // FrameXML_CreateFrames. Anything installed from a per-frame tick is therefore installed after
    // every OnLoad handler in the interface has already run.
    //
    // FrameXML_CreateFrames is the last call before the interface loads and is common to both paths,
    // which makes it the one place a global put up from outside is in force for the whole of a
    // context's life.

    /// Entry that loads a .toc and every file it names.
    constexpr uintptr_t kInterfaceLoadSeam = off::kFrameXMLCreateFrames;

    /// Its signature, for a detour and the matching trampoline.
    using InterfaceLoadFn = off::FrameXMLCreateFramesFn;

    /**
     * @brief Counts the values passed to the call.
     * @param state  Script state the call arrived on.
     * @return How many, including the frame for a method call.
     */
    inline int ArgCount(void* state)
    { return Native<off::LuaGetTopFn>(off::kLuaGetTop)(state); }

    /**
     * @brief Reports whether an argument is a number, or a string convertible to one.
     * @param state  Script state.
     * @param index  One-based argument index.
     */
    inline bool IsNumber(void* state, int index)
    { return Native<off::LuaIsNumberFn>(off::kLuaIsNumber)(state, index) != 0; }

    /**
     * @brief Reports whether an argument is a string, or a number convertible to one.
     * @param state  Script state.
     * @param index  One-based argument index.
     */
    inline bool IsString(void* state, int index)
    { return Native<off::LuaIsStringFn>(off::kLuaIsString)(state, index) != 0; }

    /**
     * @brief Reads an argument as a number.
     * @param state  Script state.
     * @param index  One-based argument index.
     * @return The value, or 0 when it is not convertible.
     */
    inline double ToNumber(void* state, int index)
    { return Native<off::LuaToNumberFn>(off::kLuaToNumber)(state, index); }

    /**
     * @brief Reads an argument as a string.
     * @param state  Script state.
     * @param index  One-based argument index.
     * @return The text, or null when it is not convertible. It belongs to the script state and is only
     *         valid until the call returns -- copy anything kept.
     */
    inline const char* ToString(void* state, int index)
    { return Native<off::LuaToStringFn>(off::kLuaToString)(state, index, nullptr); }

    /**
     * @brief Reads an argument as Lua truth.
     * @param state  Script state.
     * @param index  One-based argument index.
     * @return false for nil and for the boolean false, true for everything else -- including 0, the
     *         empty string, and any object. An index past the last argument reads as false, so an
     *         omitted flag and an explicit false are the same answer.
     */
    inline bool ToBoolean(void* state, int index)
    { return Native<off::LuaToBooleanFn>(off::kLuaToBoolean)(state, index) != 0; }

    /**
     * @brief Reads a userdata pointer off the stack.
     * @param state  Script state.
     * @param index  Stack index.
     * @return The pointer a light userdata holds, or null for any other value.
     *
     * A frame's Lua table holds its FrameScript_Object at the numeric key 0 as a light userdata, so
     * RawGetI(state, table, 0) followed by this is the step from the table Lua sees back to the C++
     * object. It is what FrameScript_GetObjectThis does for the object a script method was called on.
     */
    inline void* ToUserData(void* state, int index)
    { return Native<off::LuaToUserDataFn>(off::kLuaToUserData)(state, index); }

    /**
     * @brief Pushes a number as a return value.
     * @param state  Script state.
     * @param value  Value to return.
     */
    inline void PushNumber(void* state, double value)
    { Native<off::LuaPushNumberFn>(off::kLuaPushNumber)(state, value); }

    /**
     * @brief Pushes a string as a return value.
     * @param state  Script state.
     * @param value  Text to return; it is copied.
     */
    inline void PushString(void* state, const char* value)
    { Native<off::LuaPushStringFn>(off::kLuaPushString)(state, value); }

    /**
     * @brief Pushes a run of bytes as a string.
     * @param state   Script state.
     * @param bytes   First byte; need not be NUL terminated.
     * @param length  How many bytes.
     *
     * The value is copied, so a substring of a longer string is pushed without being copied out
     * first. Lua strings hold NUL bytes, so the length alone decides where the string ends.
     */
    inline void PushLString(void* state, const char* bytes, size_t length)
    { Native<off::LuaPushLStringFn>(off::kLuaPushLString)(state, bytes, length); }

    /**
     * @brief Pushes a boolean as a return value.
     * @param state  Script state.
     * @param value  Value to return.
     */
    inline void PushBoolean(void* state, bool value)
    { Native<off::LuaPushBooleanFn>(off::kLuaPushBoolean)(state, value ? 1 : 0); }

    /**
     * @brief Raises a script error, the way every stock binding reports a bad argument.
     * @param state   Script state.
     * @param format  Message, with Lua's own %s / %d / %f directives.
     *
     * DOES NOT RETURN. The raise leaves through longjmp, so nothing with a destructor may be alive
     * in the calling frame when it is reached.
     */
    template <class... Args>
    inline void Error(void* state, const char* format, Args... args)
    { Native<off::LuaLErrorFn>(off::kLuaLError)(state, format, args...); }

    /// The pseudo-indices and the two type tags a table walk distinguishes.
    constexpr int kRegistryIndex = off::kRegistryIndex;
    constexpr int kGlobalsIndex  = off::kGlobalsIndex;
    constexpr int kTypeNil       = off::kTypeNil;
    constexpr int kTypeTable     = off::kTypeTable;
    constexpr int kTypeFunction  = off::kTypeFunction;

    /**
     * @brief The index of the top stack slot, which is also how many values sit on the stack.
     *
     * ArgCount is the same call read as a question about a script call's arguments; this is the same
     * question asked by code that pushes scratch values and has to put the stack back.
     */
    inline int StackTop(void* state)
    { return Native<off::LuaGetTopFn>(off::kLuaGetTop)(state); }

    /**
     * @brief The type tag of a stack slot.
     * @param state  Script state.
     * @param index  Stack index; negative counts from the top.
     */
    inline int Type(void* state, int index)
    { return Native<off::LuaTypeFn>(off::kLuaType)(state, index); }

    /**
     * @brief Sets the stack top, dropping or nil-filling to reach it.
     * @param state  Script state.
     * @param index  New top; negative counts from the current top.
     *
     * A C function that pushes scratch values restores the top it was entered with rather than
     * counting pops: one early return that forgets a pop leaves the stack wrong for every later call
     * on that state.
     */
    inline void SetTop(void* state, int index)
    { Native<off::LuaSetTopFn>(off::kLuaSetTop)(state, index); }

    /// Pushes nil.
    inline void PushNil(void* state)
    { Native<off::LuaPushNilFn>(off::kLuaPushNil)(state); }

    /// Pushes a copy of the value at @p index.
    inline void PushValue(void* state, int index)
    { Native<off::LuaPushValueFn>(off::kLuaPushValue)(state, index); }

    /**
     * @brief Pops the top value and stores it at @p index.
     *
     * The one way to drop a value from under the top: SetTop only truncates, so a walk that leaves
     * scratch below its result collapses the stack with this and then truncates.
     */
    inline void Replace(void* state, int index)
    { Native<off::LuaReplaceFn>(off::kLuaReplace)(state, index); }

    /**
     * @brief Raw table read: pops the key at the top, pushes t[key].
     * @param state       Script state.
     * @param tableIndex  Stack index of the table.
     *
     * Raw, so no __index runs. A frame's Lua table carries a metatable whose __index is its method
     * table, and a raw read is how you ask what the object itself holds rather than what its class
     * would answer.
     */
    inline void RawGet(void* state, int tableIndex)
    { Native<off::LuaRawGetFn>(off::kLuaRawGet)(state, tableIndex); }

    /// Raw indexed read: pushes t[n]. This is how a registry reference is resolved to its value.
    inline void RawGetI(void* state, int tableIndex, int n)
    { Native<off::LuaRawGetIFn>(off::kLuaRawGetI)(state, tableIndex, n); }

    /// Raw table write: pops the value then the key, and sets t[key] = value. No __newindex runs.
    inline void RawSet(void* state, int tableIndex)
    { Native<off::LuaRawSetFn>(off::kLuaRawSet)(state, tableIndex); }

    /**
     * @brief Advances a table walk: pops a key, pushes the next key and its value.
     * @return 0 at the end of the table, having pushed nothing.
     *
     * Start it by pushing nil as the first key.
     */
    inline int Next(void* state, int tableIndex)
    { return Native<off::LuaNextFn>(off::kLuaNext)(state, tableIndex); }

    /**
     * @brief Pushes the value a name stands for, taking '.' as a table step.
     * @param state  Script state.
     * @param name   A global's name, or a dotted path such as "Enum.BagsDirection.Left".
     *
     * Exactly one value is pushed on every path.
     *
     * Raw at every hop, matching how the engine itself reads the global table when it looks a script
     * object up by name (FrameScript_Object::RegisterScriptObject). A name with no '.' is one raw
     * read of the global table and nothing else, which is what it has always been.
     *
     * Nil is pushed, and nothing is raised, when a hop before the last is absent or is not a table:
     * a raw read is only defined on a table, so each intermediate value is type-checked before it is
     * used as one. The value of the LAST hop is pushed whatever it is, so a path naming a missing
     * leaf pushes that leaf's own nil -- indistinguishable to the caller from a nil this function
     * substituted, and correct either way.
     *
     * An empty segment -- a leading, trailing or doubled '.' -- names no key and pushes nil.
     */
    inline void PushGlobal(void* state, const char* name)
    {
        if (!name)
        {
            PushNil(state);
            return;
        }

        const int base = StackTop(state);

        // The global table for the first hop, then whatever the hop before it left on the stack.
        int table = kGlobalsIndex;

        for (const char* segment = name;;)
        {
            const char* end = segment;
            while (*end && *end != '.') ++end;

            if (end == segment)
            {
                SetTop(state, base);
                PushNil(state);
                return;
            }

            PushLString(state, segment, static_cast<size_t>(end - segment));
            RawGet(state, table);

            if (!*end) break;

            if (Type(state, -1) != kTypeTable)
            {
                SetTop(state, base);
                PushNil(state);
                return;
            }

            table   = StackTop(state);
            segment = end + 1;
        }

        // Every hop but the last left its table behind; collapse them so the caller sees one push.
        if (StackTop(state) > base + 1)
        {
            Replace(state, base + 1);
            SetTop(state, base + 1);
        }
    }

    /**
     * @brief Pushes the script metatable the client built for texture regions.
     * @return false having pushed nothing, when no metatable is live -- the state between a context
     *         being torn down and RegisterSimpleFrameScriptMethods rebuilding it.
     *
     * Its __index is the method table every texture object answers through, so this is how a method
     * is added to every texture without an object to read the table off. That matters because a
     * texture can only be made through a frame, and no frame exists until the interface loads.
     */
    inline bool PushTextureMetatable(void* state)
    {
        const int ref = *reinterpret_cast<const int*>(off::kSimpleTextureMetaTableRef);
        if (ref <= 0) return false;

        const int base = StackTop(state);
        RawGetI(state, kRegistryIndex, ref);
        if (Type(state, -1) == kTypeTable) return true;

        SetTop(state, base);
        return false;
    }

    /**
     * @brief Registers a global script function.
     * @param name      Name it is called by.
     * @param function  Function invoked.
     *
     * For a method on a frame type, see wxl::game::glue::AddMethods instead.
     */
    inline void Register(const char* name, Function function)
    { Native<off::FrameScriptRegisterFunctionFn>(off::kFrameScriptRegisterFunction)(name, function); }

    /**
     * @brief The active script context.
     * @return The Lua state FrameScript runs on, or null before script initialization and while the
     *         client is between contexts.
     *
     * A context is torn down and rebuilt across a UI reload, so a caller that registered anything
     * into one compares the pointer rather than assuming its own registration survived.
     */
    inline void* Context()
    { return Native<off::FrameScriptGetContextFn>(off::kFrameScriptGetContext)(); }

    /**
     * @brief Compiles and runs a chunk of script source on the active context.
     * @param source     Chunk text, NUL terminated.
     * @param chunkName  Name a compile or runtime error inside it is reported against.
     *
     * THE STATE IS NOT PASSED: the engine's entry reads the active context itself. Its second
     * argument is the chunk name and its third the taint source, which is left null so the chunk
     * counts as the client's own rather than an addon's -- what the client passes for compat.lua.
     *
     * Nothing is returned: a compile or runtime failure is reported through the client's own script
     * error path, not to the caller. The call is stack-neutral either way.
     */
    inline void Execute(const char* source, const char* chunkName)
    { Native<off::FrameScriptExecuteFn>(off::kFrameScriptExecute)(source, chunkName, nullptr); }
    /**
     * @brief Calls a function already on the stack, under its arguments.
     * @param state         Script state.
     * @param argCount      How many arguments were pushed after the function.
     * @param resultCount   How many values to leave; -1 for all of them.
     * @param errorHandler  Stack index of a handler for the error object, or 0 for none.
     * @return 0 having left @p resultCount values, or non-zero having left the error object.
     *
     * Protected, so a Lua error raised inside does not unwind through the C++ frames between here
     * and the client -- which is the only reason to call Lua from inside one of its own load paths.
     */
    inline int PCall(void* state, int argCount, int resultCount, int errorHandler)
    { return Native<off::LuaPCallFn>(off::kLuaPCall)(state, argCount, resultCount, errorHandler); }

    /**
     * @brief Stores the value at the top of the stack in a table and returns a key for it.
     * @param state       Script state.
     * @param tableIndex  Table to store into; kRegistryIndex is what keeps a value alive for the
     *                    process without a global naming it.
     * @return The key, or -1 when the value was nil. The value is popped either way.
     */
    inline int Ref(void* state, int tableIndex)
    { return Native<off::LuaLRefFn>(off::kLuaLRef)(state, tableIndex); }

    /// Releases a key Ref handed out. The value stays alive for as long as something else holds it.
    inline void Unref(void* state, int tableIndex, int ref)
    { Native<off::LuaLUnrefFn>(off::kLuaLUnref)(state, tableIndex, ref); }

    /// Pushes the value a Ref key stands for.
    inline void PushRef(void* state, int ref)
    { RawGetI(state, kRegistryIndex, ref); }

    /**
     * @brief Compiles a chunk and keeps the one value it returns.
     * @param chunkName  Name errors from the chunk are reported against; the client spells a script
     *                   handler's as "*:OnLoad".
     * @param format     The chunk's source, with exactly one %s for @p body.
     * @param body       What that %s is replaced with.
     * @param status     The client's load-status object, for the error to be reported through, or
     *                   null to leave it unreported.
     * @return A registry key for the chunk's single return value, or -1 when it did not compile or
     *         did not run. Release it with Unref.
     *
     * This is the client's own compiler for XML handler bodies, so a function built this way is
     * indistinguishable from one the client built and needs no exemption from the callback
     * validator: it is Lua, not a pointer into an extension's image.
     */
    inline int CompileFunction(const char* chunkName, const char* format, const char* body,
                               void* status)
    {
        return Native<off::FrameScriptCompileFunctionFn>(off::kFrameScriptCompileFunction)(
            chunkName, format, body, status);
    }
}
