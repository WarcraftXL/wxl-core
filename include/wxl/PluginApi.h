// The extension ABI: what an out-of-core extension exports, and what the core hands it back.
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

#ifndef WXL_PLUGIN_API_H
#define WXL_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

#include "wxl/Common.h"

// The only file shared verbatim between the core and an extension. The SDK (wxl::game) is
// header-only and compiles into the extension, which reads the fixed-base client image directly, so
// what crosses here is limited to what the core has to arbitrate: hooks, events, log.
//
// Plain C with __cdecl spelled out on every pointer: the default calling convention is a compiler
// flag, and an extension built with another one would corrupt the stack on the first call. Nothing
// with a C++ ABI crosses (no std types, exceptions or vtables), so an extension may be built with a
// different toolchain or standard library.

#ifdef __cplusplus
extern "C" {
#endif

/// Bumped whenever the layout below changes. An extension records what it compiled against, and the
/// core refuses a version it cannot serve.
#define WXL_API_VERSION 1

/// The client build an extension is written against. Refusing a mismatch stops an extension full of
/// hardcoded addresses from running against an image where they mean something else.
#define WXL_CLIENT_BUILD 12340

/// Severity values accepted by WXL_Api::Log, mirroring the core's own levels.
#define WXL_LOG_TRACE 0
#define WXL_LOG_DEBUG 1
#define WXL_LOG_INFO  2
#define WXL_LOG_WARN  3
#define WXL_LOG_ERROR 4

/// Chain position for HookAttach: lower runs first, closest to the caller. Equal values keep attach
/// order.
#define WXL_HOOK_DEFAULT_PRIORITY 0

/**
 * @brief What an extension reports about itself before any of its code runs.
 *
 * Returned by WXL_Query, which must have no side effects: it is the only point at which an
 * incompatible extension can be refused having executed nothing.
 */
typedef struct WXL_PluginInfo
{
    uint32_t    structSize;    ///< sizeof(WXL_PluginInfo), so the core can tell versions apart
    uint32_t    apiVersion;    ///< WXL_API_VERSION seen at compile time
    const char* name;          ///< short display name, used in log lines
    uint32_t    pluginVersion; ///< the extension's own version, opaque to the core
    uint32_t    clientBuild;   ///< WXL_CLIENT_BUILD seen at compile time
} WXL_PluginInfo;

/**
 * @brief Event handler signature.
 * @param user  the opaque pointer given to Subscribe.
 * @param args  the event's args struct, cast to the type documented for that event.
 */
typedef void(__cdecl* WXL_EventFn)(void* user, const void* args);

/**
 * @brief A panel body, called while the core's overlay is open.
 *
 * Runs inside an already-open window, so it draws by calling the Ui* functions below and nothing
 * else. It is not called on frames where the overlay is shut, so it must not be where a module does
 * work it needs done.
 * @param user  the opaque pointer given to UiAddPanel.
 */
typedef void(__cdecl* WXL_PanelFn)(void* user);

/**
 * @brief The core's services, handed to an extension at load.
 *
 * Check structSize before reading a field an older version may not carry. The table lives for the
 * process lifetime, so the pointer can be kept.
 */
typedef struct WXL_Api
{
    uint32_t structSize;
    uint32_t apiVersion;

    /**
     * @brief Writes one line to the core's log.
     * @param level  one of the WXL_LOG_* values.
     * @param tag    prefix identifying the caller, since one table serves every extension.
     * @param fmt    printf-style format, formatted by the core.
     */
    void(__cdecl* Log)(int level, const char* tag, const char* fmt, ...);

    /**
     * @brief Subscribes to a core event for the process lifetime.
     *
     * No counterpart: removal would have to be handled inside the per-frame publish path, and
     * extensions are never unloaded.
     * @param event    event id, from wxl::events::Event.
     * @param handler  invoked on every publication of that event.
     * @param user     opaque pointer passed back to the handler.
     */
    void(__cdecl* Subscribe)(uint32_t event, WXL_EventFn handler, void* user);

    /**
     * @brief Publishes a core event to every subscriber, core or extension.
     *
     * The same bus Subscribe listens on: an extension that used to be compiled into the core and
     * relied on another in-core module's Subscribe still reaches it once both are extensions,
     * without either knowing where the other lives.
     * @param event  event id, from wxl::events::Event.
     * @param args   pointer to that event's args struct, per the catalog's documented type; the
     *               core does not interpret it, only forwards it to each handler.
     */
    void(__cdecl* Emit)(uint32_t event, const void* args);

    /**
     * @brief Adds a detour to an address, alongside any party already detouring it.
     *
     * original receives the next link in the chain, which ends at the engine function. Not calling
     * it suppresses both the parties behind and the engine function.
     * @param name      label used for logging.
     * @param target    address to detour.
     * @param detour    replacement function.
     * @param original  receives the next link in the chain.
     * @param priority  chain position; see WXL_HOOK_DEFAULT_PRIORITY.
     * @return non-zero if the detour was registered.
     */
    int(__cdecl* HookAttach)(const char* name, uintptr_t target, void* detour, void** original,
                             int priority);

    /**
     * @brief Adds a detour to a hook point the core knows by name, alongside any party already
     *        detouring it.
     *
     * Same chaining semantics as HookAttach, except the core resolves pointName against its own
     * hook-point table instead of the caller supplying a raw address -- an extension that only ever
     * attaches to named points never needs to include an offsets/ header itself.
     * @param pointName  name the core registered the hook point under.
     * @param detour     replacement function.
     * @param original   receives the next link in the chain.
     * @param priority   chain position; see WXL_HOOK_DEFAULT_PRIORITY.
     * @return non-zero if the detour was registered; zero if pointName is not a registered hook point.
     */
    int(__cdecl* HookAttachByName)(const char* pointName, void* detour, void** original, int priority);

    /**
     * @brief Offers a service to other extensions.
     *
     * The core stores the name, version and pointer without interpreting them, so two extensions can
     * agree on a capability this table does not model.
     * @param name     agreed service name.
     * @param version  agreed service version.
     * @param iface    pointer to the service, valid for the process lifetime.
     */
    void(__cdecl* PublishInterface)(const char* name, uint32_t version, void* iface);

    /**
     * @brief Looks up a service published by another extension.
     * @param name     service name.
     * @param version  required version; a different published version does not match.
     * @return the published pointer, or NULL when no extension published it.
     */
    void*(__cdecl* GetInterface)(const char* name, uint32_t version);

    // --- controls on the core's overlay ---------------------------------------------------------
    // The core owns the interface library and never lends it out. Handing an extension the library's
    // own context would tie both sides to one build of it and one C++ ABI, which is the guarantee
    // this header exists to keep. So what crosses is this: named controls, primitive types, and a
    // callback the core invokes at the point where drawing them is legal. It stays immediate mode --
    // a panel may loop, branch, and draw a different thing every frame.
    //
    // Every function below is valid only inside a WXL_PanelFn, and does nothing anywhere else.

    /**
     * @brief Registers a panel drawn whenever the overlay is open.
     *
     * Registration is for the process lifetime, like Subscribe. Call it at load: a panel costs
     * nothing on the frames its window is shut.
     * @param title  window title, and its identity to the interface library -- unique and stable.
     * @param fn     body, invoked inside an already-open window.
     * @param user   opaque pointer handed back to @p fn.
     */
    void(__cdecl* UiAddPanel)(const char* title, WXL_PanelFn fn, void* user);

    /// Non-zero while the overlay is open. For a module that wants to know without owning a panel.
    int(__cdecl* UiIsOpen)(void);

    /// Draws a line of text. Formatting is the caller's, so no varargs cross the boundary.
    void(__cdecl* UiText)(const char* text);

    /// Draws a horizontal rule.
    void(__cdecl* UiSeparator)(void);

    /// Draws a button; returns non-zero on the frame it is pressed.
    int(__cdecl* UiButton)(const char* label);

    /**
     * @brief Draws a checkbox bound to the caller's flag.
     * @param value  read for the current state and written when it changes; int, not bool, because
     *               bool has no guaranteed representation across a compiler boundary.
     * @return non-zero on the frame it changes.
     */
    int(__cdecl* UiCheckbox)(const char* label, int* value);

    /// Draws a slider bound to the caller's value, clamped to [min, max]. Non-zero when it changes.
    int(__cdecl* UiSliderFloat)(const char* label, float* value, float min, float max);

    /// Draws an integer slider bound to the caller's value. Non-zero when it changes.
    int(__cdecl* UiSliderInt)(const char* label, int* value, int min, int max);

    /// Draws a colour picker over four floats in 0..1, red first. Non-zero when it changes.
    int(__cdecl* UiColorEdit)(const char* label, float rgba[4]);

    /// Puts the next control beside the one just drawn instead of under it.
    void(__cdecl* UiSameLine)(void);

    /**
     * @brief Draws a dropdown over a list of labels.
     * @param index  read for the entry to show, written when another is picked. An index left over
     *               from a longer list is clamped rather than refused: a caller that keeps a
     *               selection across a change of subject is the ordinary case, not a mistake.
     * @param items  @p count NUL-terminated labels, read only for the duration of the call.
     * @return non-zero on the frame the selection changes.
     */
    int(__cdecl* UiCombo)(const char* label, int* index, const char* const* items, int count);

    /// Draws a section header the reader can fold away. Non-zero while it is open, which is when the
    /// caller should draw the section's body.
    int(__cdecl* UiCollapsingHeader)(const char* label);

    /**
     * @brief Draws a single-line text field bound to the caller's buffer.
     *
     * Appended after the fields above -- check structSize before reading it if built against an
     * older copy of this header may be a possibility (see the struct's own doc comment).
     * @param buf      read for the current text and written in place when it changes; always left
     *                 NUL-terminated within bufSize.
     * @param bufSize  capacity of @p buf, terminator included.
     * @return non-zero on the frame the text changes.
     */
    int(__cdecl* UiInputText)(const char* label, char* buf, size_t bufSize);
} WXL_Api;

/// The two entry points as the core resolves them, by name, out of a loaded extension.
typedef const WXL_PluginInfo*(__cdecl* WXL_QueryFn)(void);
typedef int(__cdecl* WXL_LoadFn)(const WXL_Api* api);

// Visible only to the extension being compiled: the core includes this header for the table above
// and must not export the entry points it goes looking for.
#ifdef WXL_EXTENSION

/**
 * @brief First entry point: describes the extension. Must have no side effects.
 * @return the extension's info, or NULL to decline being loaded.
 */
WXL_EXPORT const WXL_PluginInfo* __cdecl WXL_Query(void);

/**
 * @brief Second entry point: the extension sets itself up.
 *
 * Called on the main thread during engine initialisation, early enough to precede the background
 * file reader and the texture subsystem. A detour attached here is armed before the engine reaches
 * either, which is what lets an extension replace startup behaviour rather than only react to it.
 *
 * Nothing that engine initialisation itself brings up exists yet -- there is no graphics device and
 * no world. Subscribe to an event for anything that needs one.
 * @param api  the core's service table, valid for the process lifetime.
 * @return non-zero on success; zero to abort this extension's load.
 */
WXL_EXPORT int __cdecl WXL_Load(const WXL_Api* api);

#endif // WXL_EXTENSION

#ifdef __cplusplus
}
#endif

#endif // WXL_PLUGIN_API_H
