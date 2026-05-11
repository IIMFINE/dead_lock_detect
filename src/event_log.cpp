#include "event_log.h"
#include "backend.h"
#include "backtrace.h"
#include "bypass.h"
#include "config.h"
#include "real_symbols.h"
#include "ring_buffer.h"
#include "thread_state.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#include <string>
#include <time.h>
#include <unistd.h>

namespace dl {

namespace {

FILE* g_fp = nullptr;
std::atomic<bool> g_log_enabled{false};

constexpr int      kProducerSpinBeforeSleep = 32;
constexpr uint32_t kProducerSleepUs = 50;

// 与 backend.cpp 内的 EventHdrBin 严格一致。
struct EventHdrBin {
    uint64_t ts_ns;
    uint64_t tid;
    uint64_t addr;
    int64_t  rc_or_flags;
    uint8_t  op;
    uint8_t  kind;
    uint16_t frame_cnt;
    uint32_t _pad;
};
static_assert(sizeof(EventHdrBin) == 40, "EventHdrBin layout mismatch");

}  // namespace

bool log_enabled() { return g_log_enabled.load(std::memory_order_acquire); }

void log_init(const char* path) {
    if (g_fp) return;
    char final_path[512];
    const char* pct = path ? strstr(path, "%p") : nullptr;
    if (pct) {
        std::string tmp = path;
        size_t pos = tmp.find("%p");
        char pidbuf[16];
        snprintf(pidbuf, sizeof(pidbuf), "%d", getpid());
        tmp.replace(pos, 2, pidbuf);
        snprintf(final_path, sizeof(final_path), "%s", tmp.c_str());
    } else if (path && *path) {
        snprintf(final_path, sizeof(final_path), "%s", path);
    } else {
        snprintf(final_path, sizeof(final_path), "/tmp/deadlock.%d.events", getpid());
    }
    g_fp = fopen(final_path, "w");
    if (!g_fp) {
        fprintf(stderr, "[deadlock] failed to open event log: %s\n", final_path);
        return;
    }
    setvbuf(g_fp, nullptr, _IOFBF, 1 << 20);
    fprintf(g_fp, "HEADER\tDEADLOCK_EVENTS\tv2\tpid=%d\n", getpid());
    fprintf(stderr, "[deadlock] event log: %s\n", final_path);

    if (!backend_start(g_fp)) {
        fclose(g_fp);
        g_fp = nullptr;
        return;
    }
    g_log_enabled.store(true, std::memory_order_release);
}

void log_close() {
    if (!g_log_enabled.load(std::memory_order_acquire)) return;
    g_log_enabled.store(false, std::memory_order_release);

    backend_request_shutdown();
    backend_join();
    backend_final_drain();

    if (g_fp) {
        fflush(g_fp);
        fclose(g_fp);
        g_fp = nullptr;
    }
}

// fork-child 路径：不 join backend，直接释放状态。
void log_close_fast_for_fork_child() {
    if (!g_log_enabled.load(std::memory_order_acquire) && !g_fp) return;
    g_log_enabled.store(false, std::memory_order_release);
    backend_fork_child_reset();
    if (g_fp) {
        fclose(g_fp);
        g_fp = nullptr;
    }
}

static uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ull + ts.tv_nsec;
}

void log_event(EvOp op, EvKind kind, const void* addr,
               long rc_or_flags, int frame_skip, int frame_depth) {
    if (!g_log_enabled.load(std::memory_order_acquire)) return;
    ScopedBypass _bp;

    Backtrace bt = capture_backtrace(frame_skip, frame_depth);
    uint16_t frame_cnt = static_cast<uint16_t>(bt.size());

    ThreadRing* ring = current_ring();
    if (!ring) return;  // 内存分配失败；静默丢

    const size_t payload = sizeof(EventHdrBin) + sizeof(uintptr_t) * frame_cnt;

    void* dst = nullptr;
    int spins = 0;
    for (;;) {
        dst = ring->reserve(payload);
        if (dst) break;
        if (spins < kProducerSpinBeforeSleep) {
            sched_yield();
            spins++;
        } else {
            struct timespec ts{};
            ts.tv_nsec = kProducerSleepUs * 1000;
            clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
            spins = 0;
        }
    }

    EventHdrBin hdr{};
    hdr.ts_ns       = now_ns();
    hdr.tid         = current_tid();
    hdr.addr        = reinterpret_cast<uintptr_t>(addr);
    hdr.rc_or_flags = static_cast<int64_t>(rc_or_flags);
    hdr.op          = static_cast<uint8_t>(op);
    hdr.kind        = static_cast<uint8_t>(kind);
    hdr.frame_cnt   = frame_cnt;
    std::memcpy(dst, &hdr, sizeof(hdr));
    if (frame_cnt > 0) {
        std::memcpy(static_cast<char*>(dst) + sizeof(hdr),
                    bt.data(), sizeof(uintptr_t) * frame_cnt);
    }
    ring->commit();
}

}  // namespace dl