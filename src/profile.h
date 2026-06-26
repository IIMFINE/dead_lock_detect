#pragma once
//
// 轻量耗时埋点，仅在 DL_PROFILE_ENABLED=1 时生效（默认 0，无任何运行时开销）。
//
// 用法 1：RAII 作用域计时
//     DL_PROFILE_SCOPE("fwrite");
//     fwrite(...);
//
// 用法 2：条件计时（只在某个分支被命中时才记录耗时，并附带计数）
//     DL_PROFILE_COUNTER(g_wait, "reserve-wait");
//     uint64_t t0 = 0; uint32_t aux = 0;
//     for (;;) {
//         if (ok) break;
//         if (!t0) t0 = DL_PROFILE_NOW();
//         do_wait(); ++aux;
//     }
//     if (t0) DL_PROFILE_RECORD(g_wait, DL_PROFILE_NOW() - t0, aux);
//
// 每个 Counter 累计 N 次后自动 stderr 打印一行汇总（N 由 print_mask 控制）。
//

#ifndef DL_PROFILE_ENABLED
#define DL_PROFILE_ENABLED 0
#endif

#if DL_PROFILE_ENABLED

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <time.h>

namespace dl::profile {

inline uint64_t now_ns() noexcept {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ull + ts.tv_nsec;
}

struct Counter {
    const char* name;
    uint64_t print_mask;
    std::atomic<uint64_t> calls{0};     // 调用次数
    std::atomic<uint64_t> hits{0};      // 真正贡献时间的次数（dt>0）
    std::atomic<uint64_t> ns_total{0};
    std::atomic<uint64_t> ns_max{0};
    std::atomic<uint64_t> aux{0};       // 附加计数（如 yield 次数）

    constexpr Counter(const char* n, uint64_t mask) noexcept
        : name(n), print_mask(mask) {}

    void record(uint64_t dt, uint64_t aux_inc = 0) noexcept {
        uint64_t c = calls.fetch_add(1, std::memory_order_relaxed) + 1;
        if (dt > 0) {
            hits.fetch_add(1, std::memory_order_relaxed);
            ns_total.fetch_add(dt, std::memory_order_relaxed);
            aux.fetch_add(aux_inc, std::memory_order_relaxed);
            uint64_t prev = ns_max.load(std::memory_order_relaxed);
            while (dt > prev && !ns_max.compare_exchange_weak(
                       prev, dt, std::memory_order_relaxed)) {}
        }
        if ((c & print_mask) == 0) dump(c);
    }

    void dump(uint64_t c) const noexcept {
        uint64_t h  = hits.load(std::memory_order_relaxed);
        uint64_t nt = ns_total.load(std::memory_order_relaxed);
        uint64_t mx = ns_max.load(std::memory_order_relaxed);
        uint64_t ax = aux.load(std::memory_order_relaxed);
        // 剔除单次最大样本（长尾）后的平均：(total - max) / (hits - 1)
        double avg_excl_max = (h > 1) ? (double)(nt - mx) / (double)(h - 1) : 0.0;
        std::fprintf(stderr,
            "[dl-prof][%s] calls=%lu hits=%lu(%.2f%%) "
            "ns_total=%lu avg_ns/hit=%.1f avg_ns/hit(excl_max)=%.1f max_ns=%lu "
            "aux=%lu avg_aux/hit=%.2f\n",
            name,
            (unsigned long)c, (unsigned long)h,
            (double)h / (double)c * 100.0,
            (unsigned long)nt,
            h ? (double)nt / (double)h : 0.0,
            avg_excl_max,
            (unsigned long)mx,
            (unsigned long)ax,
            h ? (double)ax / (double)h : 0.0);
    }
};

struct ScopeTimer {
    Counter& c;
    uint64_t t0;
    explicit ScopeTimer(Counter& counter) noexcept : c(counter), t0(now_ns()) {}
    ~ScopeTimer() noexcept { c.record(now_ns() - t0); }
};

}  // namespace dl::profile

#define DL_PROFILE_CONCAT_(a, b) a##b
#define DL_PROFILE_CONCAT(a, b) DL_PROFILE_CONCAT_(a, b)

#ifndef DL_PROFILE_PRINT_MASK
#define DL_PROFILE_PRINT_MASK 0xFFFFu  // 每 64K 次打印一行
#endif

#define DL_PROFILE_COUNTER(var, name) \
    static ::dl::profile::Counter var((name), DL_PROFILE_PRINT_MASK)

#define DL_PROFILE_SCOPE(name)                                           \
    DL_PROFILE_COUNTER(DL_PROFILE_CONCAT(_dl_prof_c_, __LINE__), name);  \
    ::dl::profile::ScopeTimer DL_PROFILE_CONCAT(_dl_prof_t_, __LINE__)(  \
        DL_PROFILE_CONCAT(_dl_prof_c_, __LINE__))

#define DL_PROFILE_NOW() ::dl::profile::now_ns()
#define DL_PROFILE_RECORD(counter, dt_ns, aux_inc) \
    (counter).record((dt_ns), (aux_inc))

#else  // DL_PROFILE_ENABLED == 0

#define DL_PROFILE_COUNTER(var, name)         struct DL_PROFILE_CONCAT(_dl_prof_dummy_, __LINE__) {}
#define DL_PROFILE_SCOPE(name)                do {} while (0)
#define DL_PROFILE_NOW()                      ((uint64_t)0)
#define DL_PROFILE_RECORD(c, dt, aux)         do {} while (0)
#define DL_PROFILE_CONCAT_(a, b) a##b
#define DL_PROFILE_CONCAT(a, b) DL_PROFILE_CONCAT_(a, b)

#endif  // DL_PROFILE_ENABLED
