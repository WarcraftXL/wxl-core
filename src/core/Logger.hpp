// Compatibility include for runtime modules that still consume the pre-v1.1 logger path.
#pragma once

#include "common/Log.hpp"

#include <cstdarg>

namespace wxl::core::log
{
    inline void Open(const char* path) { ::wxl::log::Open(path); }

    inline void Printf(const char* fmt, ...)
    {
        if (!::wxl::log::Enabled(::wxl::log::Level::Info)) return;
        va_list args;
        va_start(args, fmt);
        ::wxl::log::WriteV(::wxl::log::Level::Info, fmt, args);
        va_end(args);
    }

    inline void Warnf(const char* fmt, ...)
    {
        if (!::wxl::log::Enabled(::wxl::log::Level::Warn)) return;
        va_list args;
        va_start(args, fmt);
        ::wxl::log::WriteV(::wxl::log::Level::Warn, fmt, args);
        va_end(args);
    }

    inline void Errorf(const char* fmt, ...)
    {
        if (!::wxl::log::Enabled(::wxl::log::Level::Error)) return;
        va_list args;
        va_start(args, fmt);
        ::wxl::log::WriteV(::wxl::log::Level::Error, fmt, args);
        va_end(args);
    }

    inline void Flush() { ::wxl::log::Flush(); }
    inline void Close() { ::wxl::log::Close(); }
}
