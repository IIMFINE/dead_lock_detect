#include "thread_state.h"
#include "bypass.h"

#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace dl {

thread_local int g_bypass_depth = 0;

static thread_local uint64_t t_tid = 0;

uint64_t current_tid() noexcept {
    if (t_tid == 0) {
        t_tid = static_cast<uint64_t>(syscall(SYS_gettid));
    }
    return t_tid;
}

void reset_thread_state_for_fork() {
    t_tid = 0;
}

}  // namespace dl
