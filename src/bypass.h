#pragma once

#include "real_symbols.h"
#include <atomic>

namespace dl {

extern thread_local int g_bypass_depth;

struct ScopedBypass {
    ScopedBypass() noexcept  { ++g_bypass_depth; }
    ~ScopedBypass() noexcept { --g_bypass_depth; }
    ScopedBypass(const ScopedBypass&) = delete;
    ScopedBypass& operator=(const ScopedBypass&) = delete;
};

inline bool should_bypass() noexcept {
    return g_bypass_depth > 0 || !real::ready();
}

}  // namespace dl
