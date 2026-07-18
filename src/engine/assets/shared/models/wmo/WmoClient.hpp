// Client contract: the WMO layout the native loader consumes (chunk tags, canonical order, field maps).
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

#include <cstdint>

#include "../../common/Chunk.hpp"

// Client contract: the WMO layout the native loader consumes. The loader walks chunks POSITIONALLY
// (assumes the canonical order, advances by each chunk size); only a trailing MCVP is magic-checked. These
// are the chunk tags it has slots for, the canonical root order, the group fixed header shape, and the
// material/flag layouts it reads. WmoSource.hpp declares the source-only additions.
namespace wxl::modern::assets::wmo
{
    using wxl::modern::assets::common::iff::FourCC;

    // Shared version chunk; consumed by the native loader.
    constexpr uint32_t kMVER = FourCC('M', 'V', 'E', 'R');

    // Root chunks the native loader has slots for.
    constexpr uint32_t kMOHD = FourCC('M', 'O', 'H', 'D'); // root header (marks a root .wmo)
    constexpr uint32_t kMOTX = FourCC('M', 'O', 'T', 'X'); // texture name blob (offsets referenced by MOMT)
    constexpr uint32_t kMOMT = FourCC('M', 'O', 'M', 'T'); // materials
    constexpr uint32_t kMOGN = FourCC('M', 'O', 'G', 'N'); // group names
    constexpr uint32_t kMOGI = FourCC('M', 'O', 'G', 'I'); // group info
    constexpr uint32_t kMOSB = FourCC('M', 'O', 'S', 'B'); // skybox name
    constexpr uint32_t kMOPV = FourCC('M', 'O', 'P', 'V'); // portal vertices
    constexpr uint32_t kMOPT = FourCC('M', 'O', 'P', 'T'); // portal info
    constexpr uint32_t kMOPR = FourCC('M', 'O', 'P', 'R'); // portal refs
    constexpr uint32_t kMOVV = FourCC('M', 'O', 'V', 'V'); // visible block vertices
    constexpr uint32_t kMOVB = FourCC('M', 'O', 'V', 'B'); // visible block list
    constexpr uint32_t kMOLT = FourCC('M', 'O', 'L', 'T'); // lights
    constexpr uint32_t kMODS = FourCC('M', 'O', 'D', 'S'); // doodad sets
    constexpr uint32_t kMODN = FourCC('M', 'O', 'D', 'N'); // doodad name blob
    constexpr uint32_t kMODD = FourCC('M', 'O', 'D', 'D'); // doodad defs
    constexpr uint32_t kMFOG = FourCC('M', 'F', 'O', 'G'); // fog
    constexpr uint32_t kMCVP = FourCC('M', 'C', 'V', 'P'); // convex volume planes (optional trailing, magic-checked)

    // Group header chunk (marks a group .wmo).
    constexpr uint32_t kMOGP = FourCC('M', 'O', 'G', 'P');

    // Group sub-chunks the native group loader knows.
    constexpr uint32_t kMOPY = FourCC('M', 'O', 'P', 'Y'); // poly material info
    constexpr uint32_t kMOVI = FourCC('M', 'O', 'V', 'I'); // vertex indices
    constexpr uint32_t kMOVT = FourCC('M', 'O', 'V', 'T'); // vertices
    constexpr uint32_t kMONR = FourCC('M', 'O', 'N', 'R'); // normals
    constexpr uint32_t kMOTV = FourCC('M', 'O', 'T', 'V'); // texture coords
    constexpr uint32_t kMOBA = FourCC('M', 'O', 'B', 'A'); // render batches
    constexpr uint32_t kMOLR = FourCC('M', 'O', 'L', 'R'); // light refs
    constexpr uint32_t kMODR = FourCC('M', 'O', 'D', 'R'); // doodad refs
    constexpr uint32_t kMOBN = FourCC('M', 'O', 'B', 'N'); // BSP tree nodes
    constexpr uint32_t kMOBR = FourCC('M', 'O', 'B', 'R'); // BSP face indices
    constexpr uint32_t kMOCV = FourCC('M', 'O', 'C', 'V'); // vertex colors
    constexpr uint32_t kMLIQ = FourCC('M', 'L', 'I', 'Q'); // liquid

    // Canonical root chunk order the positional parser expects (MOTX and MOMT carry special payloads).
    constexpr uint32_t kCanonicalRoot[] = {
        kMVER, kMOHD, kMOTX, kMOMT, kMOGN, kMOGI, kMOSB, kMOPV,
        kMOPT, kMOPR, kMOVV, kMOVB, kMOLT, kMODS, kMODN, kMODD, kMFOG,
    };

    // Material record layout (MOMT).
    constexpr uint32_t kMomtStride       = 0x40;            // bytes per material
    constexpr uint32_t kMomtShaderOffset = 0x04;            // shader id
    constexpr uint32_t kMomtTexOffsets[2] = { 0x0C, 0x18 }; // two MOTX texture-name offsets (+0x24 is a float)
    constexpr uint32_t kMomtTexCount      = 2;
    constexpr uint32_t kMaxNativeShader   = 6;              // valid shader range is 0..6
    // runTimeData (+0x30..+0x3F): device texture handles the loader fills at load. The resolver skips a
    // material whose tex1 handle (+0x38) is already non-zero, so packed junk here leaves a wild pointer
    // bound at draw. The Client ships these zero on disk; the translate clears them.
    constexpr uint32_t kMomtRunTimeOffset = 0x30;
    constexpr uint32_t kMomtRunTimeLen    = 0x10;

    // Group fixed header (MOGP). 0x44 bytes in every WMO version; sub-chunks follow it.
    constexpr uint32_t kMogpHeaderSize  = 0x44;
    constexpr uint32_t kMogpFlagsOffset = 0x08; // group flags u32 at payload+0x08

    // Batch counts inside the MOGP fixed header (u16 each).
    constexpr uint32_t kMogpTransBatchCountOffset = 0x28;
    constexpr uint32_t kMogpIntBatchCountOffset   = 0x2A;
    constexpr uint32_t kMogpExtBatchCountOffset   = 0x2C;

    // Liquid type id inside the MOGP fixed header (u32). The Client uses it as a LiquidType index.
    constexpr uint32_t kMogpGroupLiquidOffset = 0x34;

    // Group flag bit marking an exterior (outdoor-lit) group; not a gated chunk flag.
    constexpr uint32_t kGrpFlagExterior = 0x8;

    // Group flag bits the native loader gates optional sub-chunk consumption on.
    constexpr uint32_t kGrpFlagBSP   = 0x1;        // MOBN + MOBR
    constexpr uint32_t kGrpFlagMOCV  = 0x4;        // vertex colors
    constexpr uint32_t kGrpFlagMOLR  = 0x200;      // light refs
    constexpr uint32_t kGrpFlagMODR  = 0x800;      // doodad refs
    constexpr uint32_t kGrpFlagMPB   = 0x400;      // modern batch chunks (MPBV/MPBP/MPBI/MPBG)
    constexpr uint32_t kGrpFlagMLIQ  = 0x1000;     // liquid
    constexpr uint32_t kGrpFlagMORI  = 0x20000;    // triangle-strip indices (MORI + MORB)
    constexpr uint32_t kGrpFlagMOCV2 = 0x1000000;  // 2nd vertex-color set (group+0x10c)
    constexpr uint32_t kGrpFlagMOTV2 = 0x2000000;  // 2nd texcoord set (group+0xf4)

    // The ONLY flags the native loader uses to gate chunk consumption. It walks the optional sub-chunks
    // positionally by these bits in a fixed order with no magic check, so the mask MUST equal exactly the
    // loader's gated set: a bit left set whose chunk we stripped makes the parser consume a phantom chunk,
    // desync the cursor, and read a later chunk (e.g. MLIQ) from garbage -> bad count -> null buffer. The
    // translate re-derives these from the chunks it emits and PRESERVES every other bit. Source-only chunks
    // not gated by a flag are stripped physically. MPB/MORI/MOCV2/MOTV2 are gated and always cleared (those
    // chunks are stripped).
    constexpr uint32_t kGrpGatedFlags =
        kGrpFlagBSP | kGrpFlagMOCV | kGrpFlagMOLR | kGrpFlagMODR | kGrpFlagMPB | kGrpFlagMLIQ |
        kGrpFlagMORI | kGrpFlagMOCV2 | kGrpFlagMOTV2;

    // Render-batch record layout (MOBA) as the Client reads it.
    constexpr uint32_t kMobaEntryStride = 0x18;
    constexpr uint32_t kMobaBBoxOffset  = 0x00; // 6 i16: min x,y,z then max x,y,z
    constexpr uint32_t kMobaMinIndexOff = 0x12; // u16 first vertex index in this batch
    constexpr uint32_t kMobaMaxIndexOff = 0x14; // u16 last vertex index in this batch
    constexpr uint32_t kMobaMatIdOffset = 0x17; // u8 material id
    constexpr uint32_t kMovtStride      = 0x0C; // C3Vector per vertex
    constexpr uint32_t kMocvStride      = 0x04; // BGRA per vertex (byte0=B, 1=G, 2=R, 3=A)
}
