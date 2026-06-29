// test_fp_walk —— fp walk 抓栈 vs glibc ::backtrace 对照
//
// 目标：
//   1. 正确性 —— 同一调用点抓两次（一次 fp walk，一次 glibc），逐帧比对返回的 PC
//      允许 PC 有小偏移（call 后的下一条指令 vs ::backtrace 返回的可能是 call 自身或下一条）
//      只检查帧数一致 + 对应函数名一致即可
//   2. 性能 —— 各跑 N 次，独立计时，输出 avg/p50/p99/max
//
// 这个 test 不链接 deadlock_detect runtime（避免 PRELOAD 自身递归）；
// 但它会 dlopen libdeadlock_detect.so 拿到 capture_backtrace_fast 符号。

#include <dlfcn.h>
#include <execinfo.h>
#include <pthread.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using Clock = std::chrono::steady_clock;

using FpWalkFn = size_t (*)(int skip, int max_depth, uintptr_t* out, size_t cap);
static FpWalkFn fp_walk = nullptr;

// 阻止编译器把抓栈函数 inline、消除
volatile int g_sink = 0;

__attribute__((noinline))
static int leaf_glibc(void** buf, int cap) {
    int n = ::backtrace(buf, cap);
    asm volatile("" : : "r"(buf) : "memory");
    return n;
}

__attribute__((noinline))
static size_t leaf_fp(int skip, int max_depth, uintptr_t* out, size_t cap) {
    size_t n = fp_walk(skip, max_depth, out, cap);
    asm volatile("" : : "r"(out) : "memory");
    return n;
}

__attribute__((noinline))
static int recurse_glibc(int remaining, void** buf, int cap) {
    if (remaining <= 0) return leaf_glibc(buf, cap);
    int r = recurse_glibc(remaining - 1, buf, cap);
    asm volatile("" : : "r"(&r) : "memory");
    return r;
}

__attribute__((noinline))
static size_t recurse_fp(int remaining, int skip, int max_depth,
                         uintptr_t* out, size_t cap) {
    if (remaining <= 0) return leaf_fp(skip, max_depth, out, cap);
    size_t r = recurse_fp(remaining - 1, skip, max_depth, out, cap);
    asm volatile("" : : "r"(&r) : "memory");
    return r;
}

static double pct(const std::vector<uint32_t>& sorted, double p) {
    if (sorted.empty()) return 0;
    size_t idx = static_cast<size_t>(p * (sorted.size() - 1));
    return static_cast<double>(sorted[idx]);
}

static void report(const char* tag, const std::vector<uint32_t>& ns, int frames) {
    std::vector<uint32_t> s = ns;
    std::sort(s.begin(), s.end());
    long double sum = 0;
    for (auto v : s) sum += v;
    double avg = static_cast<double>(sum / static_cast<long double>(s.size()));
    std::printf("%-20s frames=%-2d  avg=%6.1f  min=%4u  p50=%6.0f  p90=%6.0f  p99=%6.0f  p999=%6.0f  max=%-7u\n",
                tag, frames, avg, s.front(), pct(s, 0.50), pct(s, 0.90),
                pct(s, 0.99), pct(s, 0.999), s.back());
}

int main(int argc, char** argv) {
    int iters       = (argc > 1) ? std::atoi(argv[1]) : 200000;
    int stack_extra = (argc > 2) ? std::atoi(argv[2]) : 16;
    int max_depth   = (argc > 3) ? std::atoi(argv[3]) : 5;
    int skip        = (argc > 4) ? std::atoi(argv[4]) : 0;

    const char* libpath = "./build/libdeadlock_detect.so";
    void* h = dlopen(libpath, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        std::fprintf(stderr, "dlopen %s failed: %s\n", libpath, dlerror());
        return 2;
    }
    // 用 mangled 名（C++ 函数）。dl 命名空间 + size_t/int/uintptr_t* 参数
    fp_walk = reinterpret_cast<FpWalkFn>(
        dlsym(h, "_ZN2dl22capture_backtrace_fastEiiPmm"));
    if (!fp_walk) {
        std::fprintf(stderr, "dlsym capture_backtrace_fast failed: %s\n", dlerror());
        return 2;
    }

    std::printf("=== test_fp_walk ===\n");
    std::printf("iters=%d stack_extra=%d max_depth=%d skip=%d\n\n",
                iters, stack_extra, max_depth, skip);

    // ---- 1) 正确性：同一调用点连续抓两次（紧挨着调用，栈位置完全相同） ----
    {
        void* glibc_buf[64];
        uintptr_t fp_buf[64];

        // 关键：把两次抓栈放在同一个 leaf 函数里调用，紧挨着
        // 这样 caller 链（一直到 main）完全相同，可以逐帧比对
        struct DualLeaf {
            __attribute__((noinline))
            static void run(void** gbuf, int gcap, int& gn,
                            uintptr_t* fbuf, int fskip, int fdepth, size_t fcap,
                            size_t& fn) {
                gn = ::backtrace(gbuf, gcap);
                asm volatile("" ::: "memory");
                fn = fp_walk(fskip, fdepth, fbuf, fcap);
                asm volatile("" ::: "memory");
            }
        };

        struct Recurse {
            __attribute__((noinline))
            static void go(int remaining, void** gbuf, int gcap, int& gn,
                           uintptr_t* fbuf, int fskip, int fdepth, size_t fcap,
                           size_t& fn) {
                if (remaining <= 0) {
                    DualLeaf::run(gbuf, gcap, gn, fbuf, fskip, fdepth, fcap, fn);
                    return;
                }
                go(remaining - 1, gbuf, gcap, gn, fbuf, fskip, fdepth, fcap, fn);
                asm volatile("" ::: "memory");
            }
        };

        int gn = 0; size_t fn = 0;
        Recurse::go(stack_extra, glibc_buf, 64, gn,
                    fp_buf, skip, max_depth, 64, fn);

        std::printf("---- correctness (same callsite) ----\n");
        std::printf("glibc returned %d frames\n", gn);
        std::printf("fp walk returned %zu frames\n", fn);

        // glibc 的第 0 帧 = leaf 中 ::backtrace 之后的指令
        // fp walk 的第 0 帧 = leaf 中 fp_walk 之后的指令
        // 两个调用紧邻，[0] PC 差几条指令；[1] 往后应该完全相同
        std::printf("compare frame-by-frame (offset by 1: glibc[i+1] vs fp[i]):\n");
        int matched = 0, total = 0;
        size_t cmp = std::min((size_t)gn - 1, fn);
        for (size_t i = 0; i < cmp && i < 8; ++i) {
            uintptr_t g = reinterpret_cast<uintptr_t>(glibc_buf[i + 1]);
            uintptr_t f = fp_buf[i];
            bool eq = (g == f);
            std::printf("  [%zu] glibc=0x%lx  fp=0x%lx  %s\n",
                        i, (unsigned long)g, (unsigned long)f,
                        eq ? "OK" : "DIFF");
            total++;
            if (eq) matched++;
        }
        std::printf("matched %d/%d frames (excluding leaf [0])\n\n", matched, total);
    }

    // ---- 2) 性能：循环抓 N 次 ----
    std::vector<uint32_t> ns_glibc(iters), ns_fp(iters);

    // glibc backtrace
    {
        void* buf[64];
        int last_n = 0;
        for (int i = 0; i < iters; ++i) {
            auto t0 = Clock::now();
            int n = recurse_glibc(stack_extra, buf, max_depth + skip + 2);
            auto t1 = Clock::now();
            last_n = n;
            g_sink += n;
            auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            ns_glibc[i] = (dt > UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(dt);
        }
        report("glibc ::backtrace", ns_glibc, last_n);
    }

    // fp walk
    {
        uintptr_t buf[64];
        size_t last_n = 0;
        for (int i = 0; i < iters; ++i) {
            auto t0 = Clock::now();
            size_t n = recurse_fp(stack_extra, skip, max_depth, buf, 64);
            auto t1 = Clock::now();
            last_n = n;
            g_sink += (int)n;
            auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            ns_fp[i] = (dt > UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(dt);
        }
        report("fp walk", ns_fp, (int)last_n);
    }

    dlclose(h);
    return 0;
}
