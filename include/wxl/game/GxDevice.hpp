// gx device bindings: what a CGxDevice backend reads, writes and calls on the engine's device object.
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
#include <cstddef>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/engine/GxDevice.hpp"

/**
 * @brief The engine device object as a backend sees it.
 *
 * A backend registered through WXL_GraphicsDeviceApi *is* the engine's CGxDevice: the engine reads and
 * writes its fields directly and calls the base class on it. This names those fields and the shared
 * engine functions, so a backend never spells a raw offset -- which is also what keeps it on the right
 * side of the SDK boundary. The layout constants are reachable as wxl::game::gxdevice::layout.
 *
 * Everything here reads or writes memory the engine owns. None of it is valid before the device object
 * has been through the base constructor (WXL_GraphicsDeviceApi::InitBaseDevice).
 */
namespace wxl::game::gxdevice
{
    namespace layout = wxl::offsets::engine::gxdevice;
    namespace gxoff  = wxl::offsets::engine::gx;

    /**
     * @brief A field of an engine record (device, format, pool, buffer, texture, shader).
     * @param object  the record.
     * @param offset  a layout:: constant.
     */
    template <class T>
    inline T& Field(void* object, size_t offset)
    {
        return wxl::game::At<T>(object, offset);
    }

    template <class T>
    inline const T& Field(const void* object, size_t offset)
    {
        return wxl::game::At<T>(object, offset);
    }

    /// Non-zero while the device is usable. Every slot that touches the API returns early when it is 0.
    inline bool HasContext(const void* device) { return Field<uint32_t>(device, layout::kContext) != 0; }

    // --- render states ----------------------------------------------------------------------------------
    // The engine keeps its own render states and flushes the dirty ones through vtable slot 1 before a
    // draw. A backend either implements that slot or reads the table here at draw time; the table is
    // reached through a POINTER on the device, so these accessors take the step for the caller.

    /// The first byte of state @p id's 16-byte value, or null before the base constructor ran.
    inline const void* RenderStateValue(const void* device, unsigned id)
    {
        const auto* table = Field<const uint8_t*>(device, layout::kRsTable);
        if (!table || id >= layout::kRenderStateCount) return nullptr;
        return table + id * layout::kRsStride + layout::kRsValue;
    }

    /// State @p id read as T (int32_t, float or a pointer). Zero-initialised when the table is absent.
    template <class T>
    inline T RenderState(const void* device, unsigned id)
    {
        const void* value = RenderStateValue(device, id);
        return value ? *static_cast<const T*>(value) : T{};
    }

    /// The master-enable bitmask: bit N off forces state N's category to its disabled value.
    inline uint32_t MasterEnables(const void* device)
    {
        return Field<uint32_t>(device, gxoff::kMasterEnableField);
    }

    /// Whether master-enable bit @p bit is set.
    inline bool MasterEnabled(const void* device, unsigned bit)
    {
        return (MasterEnables(device) & (1u << bit)) != 0;
    }

    // --- transforms -------------------------------------------------------------------------------------
    /// The top matrix of stack @p xform (a layout::kXform* index) as 16 row-major floats.
    inline const float* XformTop(const void* device, unsigned xform)
    {
        const auto* stack = static_cast<const uint8_t*>(device) + layout::kMatrixStacks
                          + xform * layout::kMatrixStackStride;
        const uint32_t top = *reinterpret_cast<const uint32_t*>(stack + layout::kMatrixStackTop);
        return reinterpret_cast<const float*>(stack + layout::kMatrixStackBase + top * 16 * sizeof(float));
    }

    /// Whether that top matrix is flagged identity, which lets a backend skip multiplying by it.
    inline bool XformTopIsIdentity(const void* device, unsigned xform)
    {
        const auto* stack = static_cast<const uint8_t*>(device) + layout::kMatrixStacks
                          + xform * layout::kMatrixStackStride;
        const uint32_t top = *reinterpret_cast<const uint32_t*>(stack + layout::kMatrixStackTop);
        return (*reinterpret_cast<const uint32_t*>(stack + layout::kMatrixStackFlags + top * 4) & 1u) != 0;
    }

    /// The projection in the API's own depth range, which vtable slot 40 derives and stores.
    inline const float* ApiProjection(const void* device)
    {
        return &Field<const float>(device, layout::kApiProjection);
    }

    // --- vertex layout ----------------------------------------------------------------------------------
    /// Descriptor of attribute slot @p attrib: {attribute, data format, offset, stride}.
    inline const uint32_t* AttribSlot(const void* device, unsigned attrib)
    {
        return &Field<const uint32_t>(device, layout::kAttribSlots + attrib * layout::kAttribSlotStride);
    }

    /// The CGxBuf feeding attribute @p attrib, or null when nothing is bound to it.
    inline void* AttribBuf(const void* device, unsigned attrib)
    {
        return Field<void*>(const_cast<void*>(device), layout::kAttribBufs + attrib * 4);
    }

    /// Bit per attribute: which slots the current vertex layout uses.
    inline uint32_t AttribMask(const void* device) { return Field<uint32_t>(device, layout::kAttribEnabled); }

    // --- engine functions every stock backend shares ------------------------------------------------------
    /// The engine window procedure every backend window uses; it chains to the client's own.
    inline void* WindowProc() { return reinterpret_cast<void*>(layout::kWindowProc); }

    /// Registers the engine window class. @return its atom, 0 on failure.
    inline uint16_t RegisterWindowClass()
    {
        return Native<layout::WindowClassCreateFn>(layout::kWindowClassCreate)();
    }

    /**
     * @brief Creates the game window the way the stock backends do.
     * @param device  the device; it becomes the window's creation parameter and gets the HWND.
     * @param format  a CGxFormat, adjusted in place for the fullscreen-window styles.
     * @return true when the window exists.
     */
    inline bool CreateGameWindow(void* device, void* format)
    {
        return Native<layout::CreateWindowFn>(layout::kCreateWindow)(device, nullptr, format);
    }

    /// Sets defWindow and curWindow from a CRect {minY, minX, maxY, maxX}.
    inline void SetDefWindow(void* device, const float rect[4])
    {
        Native<layout::DeviceSetDefWindowFn>(gxoff::kDeviceSetDefWindow)(device, nullptr, rect);
    }

    /// Sleeps to honour the client's frame-rate caps; the stock backends call it right before presenting.
    inline void WaitForFpsCap(void* device)
    {
        Native<layout::WaitForFpsCapFn>(layout::kWaitForFpsCap)(device, nullptr);
    }

    /// Runs CGxDevice::~CGxDevice on the object; the memory stays the caller's.
    inline void DestroyBase(void* device)
    {
        Native<layout::DeviceDtorFn>(layout::kDeviceDtor)(device, nullptr);
    }
}
