// The client database runtime: the WowClientDB storage layout, the startup load table, and the
// spell store's own loader and parser.
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
#include <cstdint>

// INTERNAL to the core. The .dbc storage runtime, as opposed to the per-table record offsets in
// game/DB2.hpp. Modules never include this; they use wxl::game.
//
// Every .dbc the client reads is a link-time global instance of the WowClientDB<T> template from
// Source/DB/WowClientDB.h. The template is instantiated once per record type, so Load, LoadRecords
// and the record parser exist 237 times over, each with its table's column count and row size as
// hardcoded immediates and a direct call to its own GetFilename. That is the fact that shapes
// everything here: there is no generic loader to intercept, and no column-type format string
// anywhere in the client -- the parse is a generated, fully unrolled function per table.
namespace wxl::offsets::game::dbc
{
    // -------------------------------------------------------------------------------------------
    // The storage object. 0x24 bytes in both variants below, laid out in .data by the linker with a
    // 0x24 stride and no runtime constructor: maxId starts at -1 and minId at 0x0FFFFFFF in the
    // static image, which is how an unloaded store is recognised.
    // -------------------------------------------------------------------------------------------
    constexpr size_t kSize = 0x24;

    constexpr size_t kOffVtable      = 0x00; // 8 slots; see the kSlot* indices below
    constexpr size_t kOffLoaded      = 0x04; // int, 1 once Load has finished
    constexpr size_t kOffRecordCount = 0x08; // int, read straight out of the file header
    constexpr size_t kOffMaxId       = 0x0C; // int, -1 before load
    constexpr size_t kOffMinId       = 0x10; // int, 0x0FFFFFFF before load
    constexpr size_t kOffStringTable = 0x14; // char*, the file's string block, allocated whole

    // +0x18 is where the two variants part. A standard store keeps a second, two-slot vtable here
    // whose slot 1 is the by-id accessor, reached with `this` pointing at +0x18 rather than at the
    // object -- which is why that accessor reads minId at this-8 and maxId at this-0xC.
    constexpr size_t kOffVtableById = 0x18; // standard stores: == vtable - 8
    // A compressible store has no second vtable and uses the slot for the record block instead.
    constexpr size_t kOffRecordBlock = 0x18; // compressible stores: T[] raw, or the RLE blob

    // +0x1C and +0x20 also differ in kind, not just in name.
    constexpr size_t kOffRecords = 0x1C; // standard: T[recordCount], contiguous
    constexpr size_t kOffRowPtrs = 0x1C; // compressible: void*[recordCount], by row index
    // The by-id table, indexed by (id - minId), zero where no record carries that id. The record id
    // is always field 0 of the record.
    //
    // In a compressible store this is an INTERIOR pointer into the kOffRowPtrs allocation
    // (== rowPtrs + recordCount) and is not freed separately. Replacing one means replacing both.
    constexpr size_t kOffRecordsById = 0x20;

    // Slots of the object's own vtable at kOffVtable.
    constexpr size_t kSlotDtor             = 0; ///< scalar deleting destructor
    constexpr size_t kSlotLoad             = 1; ///< Load(allocFile, allocLine)
    constexpr size_t kSlotUnused           = 2; ///< an empty virtual, the same one for every store
    constexpr size_t kSlotLoadRecords      = 3; ///< LoadRecords(file, allocFile, allocLine)
    constexpr size_t kSlotGetRecordByIndex = 4; ///< GetRecordByIndex(row, out)
    constexpr size_t kSlotFreeStorage      = 5;
    constexpr size_t kSlotReset            = 6;
    constexpr size_t kSlotRecordsById      = 7; ///< returns &recordsById

    /// Load, as every store's vtable slot 1 holds it. __thiscall; the two stack arguments are the
    /// allocation-site tag the store passes down to the engine allocator, not data.
    using LoadFn = void(__fastcall*)(void* store, void* edx, const char* allocFile, int allocLine);
    /// LoadRecords, vtable slot 3. Allocates the record storage, parses every record, then builds
    /// minId, maxId and the by-id table.
    using LoadRecordsFn = void(__fastcall*)(void* store, void* edx, void* file, const char* allocFile,
                                            int allocLine);

    // -------------------------------------------------------------------------------------------
    // Startup
    // -------------------------------------------------------------------------------------------
    /// The one call that brings the database runtime up: it settles the compression flag below,
    /// loads all 237 stores, then builds the derived game tables. Called once, from the client's
    /// own init. A five-byte `push imm32` prologue, so a detour here needs no instruction fixup.
    constexpr uintptr_t kInitialize = 0x00634E00;
    constexpr uintptr_t kShutdown   = 0x00634C60;

    /// The 237 loads, one after another: 17 bytes each, `push line / push file / mov ecx, store /
    /// call esi`, then a ret. ESI is set by the caller, which is what makes kLoadDispatchSlot a
    /// single-write choke point over every one of them.
    constexpr uintptr_t kLoadAll = 0x006337D0;

    /// The dispatch thunk kInitialize loads into ESI before calling kLoadAll: `mov eax,[ecx]` then
    /// `jmp [eax+4]`, i.e. a tail call into the store's own vtable slot 1.
    constexpr uintptr_t kLoadDispatch = 0x006348B0;
    /// The imm32 of that `mov esi, kLoadDispatch`. Overwriting these four bytes redirects all 237
    /// loads to one function that receives the store in ECX -- the cheapest way to wrap every load
    /// at once. It does not cover the startup-strings store, which is loaded by a direct call well
    /// before kInitialize runs.
    constexpr uintptr_t kLoadDispatchSlot = 0x00634E2B;

    // -------------------------------------------------------------------------------------------
    // In-memory record compression. Four stores -- spell, item display info and the two light
    // bands -- keep their records RLE-packed in memory when this flag is set, and then a by-id
    // lookup yields packed bytes rather than a record: every consumer unpacks into a local copy
    // first.
    //
    // It is set from the dbCompress console variable, whose shipped default is "-1", which this
    // code turns into 1. So compression is ON unless something sets it otherwise, and anything that
    // wants to deal in plain records has to write 0 here BEFORE kInitialize runs.
    // -------------------------------------------------------------------------------------------
    constexpr uintptr_t kCompressFlag = 0x00C5DEA0; // one byte

    /// RLE pack: (src, len, dst) -> packed length. A null dst measures without writing, which is
    /// how the load sizes its allocation before filling it.
    constexpr uintptr_t kRlePack   = 0x0095D980;
    /// RLE unpack: (src, len, dst).
    constexpr uintptr_t kRleUnpack = 0x004CFBB0;

    // -------------------------------------------------------------------------------------------
    // Localized strings. A localized column is 17 columns in the file -- 16 locale offsets and a
    // mask -- and collapses to ONE pointer in the record, chosen by this index at parse time. That
    // collapse is why an in-memory record is much smaller than a file row.
    // -------------------------------------------------------------------------------------------
    constexpr uintptr_t kCurrentLanguage = 0x00C5DE9C; // int, the locale slot a string resolves to
    /// The empty string a string field is pointed at when the file carries no string block, so a
    /// consumer never sees a null pointer.
    constexpr uintptr_t kEmptyString = 0x009E14FF;

    // -------------------------------------------------------------------------------------------
    // The spell store. A compressible store: read kOffRecordBlock / kOffRowPtrs, not kOffRecords.
    // -------------------------------------------------------------------------------------------
    namespace spell
    {
        constexpr uintptr_t kStore = 0x00AD49D0;

        /// Load, also reachable as slot 1 of the store's vtable. Nothing but the vtable refers to
        /// it, so swapping that slot is enough to replace it.
        constexpr uintptr_t kLoad = 0x00650940;
        /// The vtable slot holding kLoad, in .rdata: write here to replace the loader.
        constexpr uintptr_t kLoadVtableSlot = 0x00A28C54;

        constexpr uintptr_t kLoadRecords = 0x006608B0;

        /// The record parser: one record from the open file into the record, with the string block
        /// base as its second argument. This is the whole of how a spell row becomes a record --
        /// 169 reads in file order, then the four localized pointers -- and so the one place where
        /// the file layout the client accepts is actually decided.
        constexpr uintptr_t kReadRecord = 0x008A65E0;
        using ReadRecordFn = int(__fastcall*)(void* record, void* edx, void* file, char* stringTable);

        /// Returns "DBFilesClient\Spell.dbc". A leaf returning a literal; replace it to read the
        /// rows from another file.
        constexpr uintptr_t kGetFilename = 0x008A65D0;

        /// The by-id accessor: (id, out) -> non-zero when found, copying the record into the
        /// caller's buffer -- unpacking it when kCompressFlag is set, otherwise a flat copy of
        /// kRecordSize bytes.
        ///
        /// Hooking this intercepts only a fraction of lookups: most consumers inline the bounds
        /// check and the by-id load instead of calling it. It is also what makes the record size
        /// below hard to grow -- a consumer's buffer is a fixed-size local in its own frame.
        constexpr uintptr_t kGetRecord = 0x004CFD20;
        using GetRecordFn = int(__fastcall*)(void* store, void* edx, int id, void* out);

        /// The record in memory. 170 dwords: the file's 234 columns, less the 68 columns of the four
        /// localized groups, plus the one pointer each collapses to.
        constexpr size_t   kRecordSize   = 0x2A8; // 680
        constexpr uint32_t kRecordDwords = 170;

        /// What the file must be for kLoad to accept it, as the immediates it compares against.
        constexpr uint32_t kFileColumns    = 234;
        constexpr uint32_t kFileRecordSize = 0x3A8; // 936

        /// The imm32 of each of those two comparisons, and of the matching value in the error
        /// message that reports the mismatch. A file of another shape needs all four rewritten:
        /// kLoad has no error return, so a mismatch is a fatal dialog, not a failure to handle.
        constexpr uintptr_t kFileColumnsCmpSlot      = 0x00650A6E;
        constexpr uintptr_t kFileColumnsExpectedSlot = 0x00650A75;
        constexpr uintptr_t kRecordSizeCmpSlot       = 0x00650AC7;
        constexpr uintptr_t kRecordSizeExpectedSlot  = 0x00650ACE;

        /// The four localized string pointers in the record. Their order follows the file's column
        /// order, which makes them name, rank, description and tooltip; the offsets are read off the
        /// parser, the names are the documented Spell.dbc column order rather than anything the
        /// binary states.
        constexpr size_t kOffName        = 0x220;
        constexpr size_t kOffRank        = 0x224;
        constexpr size_t kOffDescription = 0x228;
        constexpr size_t kOffTooltip     = 0x22C;
    }
}
