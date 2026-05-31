#include "real_symbols.h"
#include "config.h"
#include "event_log.h"
#include "thread_state.h"
#include "bypass.h"
#include "backend.h"

#include <pthread.h>
#include <signal.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>

namespace {

void atfork_child() {
    dl::reset_thread_state_for_fork();
    // 子进程不继承 backend 线程，走 fast-close：跳过 join 直接释放
    dl::log_close_fast_for_fork_child();
}

void signal_flush_handler(int) {
    // 只设 flag，由 backend 主循环在下一轮 fflush
    dl::backend_request_flush();
}

std::atomic<int> g_closed{0};
void atexit_handler() {
    if (g_closed.exchange(1)) return;
    dl::log_close();
}

}  // namespace

__attribute__((constructor(101)))
static void deadlock_lib_init() {
    dl::real::init_once();
    dl::init_config_from_env();

    if (!dl::config().disabled) {
        dl::log_init(dl::config().trace_path);
        atexit(atexit_handler);
        pthread_atfork(nullptr, nullptr, atfork_child);

        int sig = dl::config().dump_signal;
        if (sig > 0) {
            struct sigaction sa;
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = signal_flush_handler;
            sa.sa_flags = SA_RESTART;
            sigaction(sig, &sa, nullptr);
        }
    }
}

__attribute__((destructor))
static void deadlock_lib_fini() {
    atexit_handler();
}
