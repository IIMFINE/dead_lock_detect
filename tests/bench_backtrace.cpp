// bench_backtrace —— 测 glibc ::backtrace() 自己的耗时
//
// 目的：把"抓栈"这一步从 log_event 里完全剥离出来单独量。
// 三个维度：
//   A) buffer 上限：depth = 0/1/2/4/8/16/32/64
//   B) 实际可走的栈深度：通过 noinline 递归把 backtrace 调用点放到 4/16/64 层深的位置
//   C) 并发：1/4/8 线程同时抓栈，看 _Unwind_Backtrace 的共享路径是否有争用
//
// 计时用 std::chrono::steady_clock（CLOCK_MONOTONIC，vDSO），每次抓栈单独测，
// 样本写到预分配缓存避免热路径分配。
//
// 用法：bench_backtrace [iters_per_thread]
//   默认：100000

#include <execinfo.h>
#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// 全局 sink 阻止编译器把 ::backtrace 的结果优化掉
volatile int g_sink = 0;

// ---- 让 stack 真的有指定深度：noinline 递归 ----
// __attribute__((noinline)) 防止 -O2 把递归吃成尾调用，clobber 列表 ("memory") 进一步抑制重排。

__attribute__((noinline))
int run_backtrace_here(void** buf, int cap) {
    int n = ::backtrace(buf, cap);
    asm volatile("" : : "r"(buf) : "memory");
    return n;
}

__attribute__((noinline))
int recurse(int remaining, void** buf, int cap) {
    if (remaining <= 0) return run_backtrace_here(buf, cap);
    int r = recurse(remaining - 1, buf, cap);
    asm volatile("" : : "r"(&r) : "memory");
    return r;
}

// ---- 测一组样本 ----
struct WorkerCtx {
    int iters;
    int cap;             // 给 ::backtrace 的 buffer size
    int stack_extra;     // 额外的递归深度
    std::vector<uint32_t> ns;
    int last_n = 0;      // 最后一次实际返回的帧数（用于报告）
};

void* worker(void* arg) {
    auto* c = static_cast<WorkerCtx*>(arg);
    const int N = c->iters;
    const int cap = c->cap;
    const int extra = c->stack_extra;

    // buffer 给到 256 上限，避免 cap=0 时还要 alloc
    void* buf[256];
    int dummy = 0;

    for (int i = 0; i < N; ++i) {
        auto t0 = Clock::now();
        int n = recurse(extra, buf, cap);
        auto t1 = Clock::now();

        c->last_n = n;
        dummy += n;

        auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        c->ns[i] = (dt > UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(dt);
    }
    g_sink = dummy;
    return nullptr;
}

double pct(const std::vector<uint32_t>& sorted, double p) {
    if (sorted.empty()) return 0;
    size_t idx = static_cast<size_t>(p * (sorted.size() - 1));
    return static_cast<double>(sorted[idx]);
}

void run_case(const char* tag, int threads, int cap, int stack_extra, int iters) {
    std::vector<WorkerCtx> ctxs(threads);
    for (auto& c : ctxs) {
        c.iters = iters;
        c.cap = cap;
        c.stack_extra = stack_extra;
        c.ns.resize(iters);
    }

    std::vector<pthread_t> tids(threads);
    auto wall0 = Clock::now();
    for (int i = 0; i < threads; ++i) {
        pthread_create(&tids[i], nullptr, worker, &ctxs[i]);
    }
    for (int i = 0; i < threads; ++i) {
        pthread_join(tids[i], nullptr);
    }
    auto wall1 = Clock::now();
    double wall_s = std::chrono::duration<double>(wall1 - wall0).count();

    std::vector<uint32_t> all;
    all.reserve(static_cast<size_t>(threads) * iters);
    for (auto& c : ctxs) all.insert(all.end(), c.ns.begin(), c.ns.end());
    std::sort(all.begin(), all.end());

    long double sum = 0;
    for (auto v : all) sum += v;
    double avg = static_cast<double>(sum / static_cast<long double>(all.size()));

    std::printf("%-40s  threads=%d  cap=%2d  +depth=%2d  actual_n=%-3d  "
                "avg=%6.0f  p50=%6.0f  p90=%6.0f  p99=%6.0f  p999=%6.0f  max=%-7u  wall=%.3fs\n",
                tag, threads, cap, stack_extra, ctxs[0].last_n,
                avg, pct(all, 0.50), pct(all, 0.90),
                pct(all, 0.99), pct(all, 0.999), all.back(), wall_s);
}

}  // namespace

int main(int argc, char** argv) {
    int iters = (argc > 1) ? std::atoi(argv[1]) : 100000;

    std::printf("=== bench_backtrace (per-call ::backtrace cost, ns) ===\n");
    std::printf("iters_per_thread=%d\n\n", iters);

    // A) buffer 上限扫描，单线程，固定栈深度（额外递归 16 层）
    std::puts("---- A) sweep cap (single thread, +depth=16) ----");
    for (int cap : {0, 1, 2, 4, 8, 16, 32, 64}) {
        char tag[64];
        std::snprintf(tag, sizeof(tag), "cap=%d", cap);
        run_case(tag, 1, cap, 16, iters);
    }

    // B) 实际栈深度扫描，单线程，cap=64
    std::puts("\n---- B) sweep actual stack depth (single thread, cap=64) ----");
    for (int extra : {0, 4, 16, 64}) {
        char tag[64];
        std::snprintf(tag, sizeof(tag), "+depth=%d", extra);
        run_case(tag, 1, 64, extra, iters);
    }

    // C) 并发扫描，cap=16，+depth=16
    std::puts("\n---- C) sweep threads (cap=16, +depth=16) ----");
    for (int t : {1, 2, 4, 8}) {
        char tag[64];
        std::snprintf(tag, sizeof(tag), "threads=%d", t);
        run_case(tag, t, 16, 16, iters);
    }

    return 0;
}
