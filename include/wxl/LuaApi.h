// Lua service: global script functions and settings sets an extension adds to the client, in the
// glue screen and in game alike.
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

#ifndef WXL_LUA_API_H
#define WXL_LUA_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WXL_LUA_API_NAME "wxl.lua"
#define WXL_LUA_API_VERSION 1

/*
 * WHERE THE FUNCTIONS LIVE
 *   The client builds a fresh script context for the glue screen and for the game, and again on
 *   every /reload; each time it loads the interface through FrameXML_CreateFrames. The core puts every
 *   function registered here into each context just before that load, so the interface's own OnLoad
 *   handlers already see them, and lets them past the engine's check that refuses any script callback
 *   outside Wow.exe ("Invalid function pointer") -- for the registered pointers only.
 *
 * TWO WAYS IN
 *   1. RegisterFunction: a plain Lua C function under a global name of your own (prefix it, WXL_...).
 *      It reads its arguments with the helpers below (one-based indices), pushes its results and
 *      returns how many it pushed.
 *   2. RegisterSettings: a named set of player settings (key -> boolean / number / string) behind
 *      five callbacks. Nothing to write in Lua: the core's own functions reach every set by name, so a
 *      Video Options panel for any extension is written against one API:
 *
 *        WXL_Settings_List()                    -> { "foliage", "postfx", ... }  the sets registered
 *        WXL_Settings_IsAvailable(set)          -> bool   false: the set exists but cannot work here
 *                                                         (wrong device, ...); also false for an
 *                                                         unknown set
 *        WXL_Settings_Get(set, key)             -> value, or nil for an unknown set or key
 *        WXL_Settings_Set(set, key, value)      -> bool   applied live
 *        WXL_Settings_GetAll(set)               -> { key = value, ... } or nil
 *        WXL_Settings_ApplyPreset(set, name)    -> bool
 *        WXL_Settings_Save(set)                 -> bool   persisted by the set's own Save
 *
 *      None of them raises: a bad set, key or value answers nil / false, so a panel written against
 *      them can only fail softly. A panel tests `WXL_Settings_Get ~= nil` first, which is false on a
 *      client without WarcraftXL.
 *
 * Everything runs on the main thread, inside a script call. Published at boot, so an extension can
 * register from its load. Error() raises a Lua error and does not return; nothing with a destructor
 * may be alive in the calling frame when it is reached.
 */

/** A script function: returns the count of values it pushed. */
typedef int(__cdecl* WXL_LuaFunction)(void* state);

/** A setting's value as a settings set hands it over. */
enum
{
    WXL_SETTING_NIL     = 0,
    WXL_SETTING_BOOLEAN = 1,
    WXL_SETTING_NUMBER  = 2,
    WXL_SETTING_STRING  = 3,
};

typedef struct WXL_SettingValue
{
    int32_t     type;      /**< WXL_SETTING_* */
    int32_t     boolean;   /**< WXL_SETTING_BOOLEAN */
    double      number;    /**< WXL_SETTING_NUMBER */
    const char* string;    /**< WXL_SETTING_STRING; Get: must stay valid until the next call into the set */
} WXL_SettingValue;

/**
 * A named set of player settings. The struct and everything it points at must outlive the process
 * (a static). Every callback gets `user` back.
 */
typedef struct WXL_SettingsSet
{
    uint32_t           structSize;
    const char*        name;       /**< what Lua names the set by, e.g. "foliage" */
    void*              user;
    const char* const* keys;       /**< the keys GetAll walks, in order */
    uint32_t           keyCount;

    /** Fills `out`; returns non-zero for a known key. */
    int(__cdecl* Get)(void* user, const char* key, WXL_SettingValue* out);
    /** Applies a value live; returns non-zero when taken. */
    int(__cdecl* Set)(void* user, const char* key, const WXL_SettingValue* value);
    /** Applies a named preset; returns non-zero when known. May be null. */
    int(__cdecl* ApplyPreset)(void* user, const char* preset);
    /** Persists the set; returns non-zero when written. May be null. */
    int(__cdecl* Save)(void* user);
    /** Non-zero when the set can work in this session. May be null (always available). */
    int(__cdecl* IsAvailable)(void* user);
} WXL_SettingsSet;

typedef struct WXL_LuaApi
{
    uint32_t structSize;
    uint32_t apiVersion;

    /**
     * @brief Adds a global script function to every context from now on (and to the live one).
     * @param name  the global it is called by; copied (63 characters at most).
     * @param fn    the function.
     * @return non-zero when registered; zero for a null argument or a full registry (256). A name
     *         registered again takes the new function.
     */
    int(__cdecl* RegisterFunction)(const char* name, WXL_LuaFunction fn);

    /**
     * @brief Adds a settings set the WXL_Settings_* functions reach by its name.
     * @return non-zero when registered; zero for a bad set or a full table (32). A name registered
     *         again takes the new set.
     */
    int(__cdecl* RegisterSettings)(const WXL_SettingsSet* set);

    /* --- arguments --- */
    int(__cdecl* ArgCount)(void* state);
    /** The Lua type of an argument: -1 none, 0 nil, 1 boolean, 3 number, 4 string, 5 table, 6 function. */
    int(__cdecl* Type)(void* state, int index);
    double(__cdecl* ToNumber)(void* state, int index);
    /** Valid until the call returns; copy anything kept. Null when not convertible. */
    const char*(__cdecl* ToString)(void* state, int index);
    /** Lua truth: false for nil and false only. */
    int(__cdecl* ToBoolean)(void* state, int index);

    /* --- results --- */
    void(__cdecl* PushNil)(void* state);
    void(__cdecl* PushNumber)(void* state, double value);
    void(__cdecl* PushString)(void* state, const char* value);
    void(__cdecl* PushBoolean)(void* state, int value);
    /** Pushes a new, empty table. */
    void(__cdecl* NewTable)(void* state);
    /** On the table at the top of the stack: t[key] = number / string / boolean. The table stays on top. */
    void(__cdecl* SetFieldNumber)(void* state, const char* key, double value);
    void(__cdecl* SetFieldString)(void* state, const char* key, const char* value);
    void(__cdecl* SetFieldBoolean)(void* state, const char* key, int value);

    /** Raises a Lua error with this message. DOES NOT RETURN. */
    void(__cdecl* Error)(void* state, const char* message);
} WXL_LuaApi;

#ifdef __cplusplus
}
#endif

#endif /* WXL_LUA_API_H */
