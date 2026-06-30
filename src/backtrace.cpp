#include "backtrace.h"
#include "bypass.h"
#include "profile.h"

#include <alloca.h>
#include <execinfo.h>
#include <pthread.h>
#include <cstdint>
#include <cstring>

namespace dl {

// ------------- 抓栈 -------------
Backtrace capture_backtrace(int skip, int max_depth) {
    if (max_depth <= 0) return {};
    // 抓栈内部不能被我们自己重新劫持
    ScopedBypass _b;

    const int total = skip + max_depth + 2;
    void** buf;
    {
        DL_PROFILE_SCOPE("bt/alloca");
        buf = static_cast<void**>(alloca(sizeof(void*) * total));
    }
    int n;
    {
        DL_PROFILE_SCOPE("bt/glibc_backtrace");
        n = ::backtrace(buf, total);
    }
    DL_PROFILE_COUNTER(prof_nframes, "bt/n_frames");
    DL_PROFILE_RECORD(prof_nframes, 1, n);
    int start = skip < n ? skip : n;
    Backtrace bt;
    {
        DL_PROFILE_SCOPE("bt/vec_reserve");
        bt.reserve(n - start);
    }
    {
        DL_PROFILE_SCOPE("bt/vec_fill");
        for (int i = start; i < n; ++i) {
            bt.push_back(reinterpret_cast<uintptr_t>(buf[i]));
        }
    }
    return bt;
}

// 快速 frame-pointer 抓栈：不走 glibc backtrace（避开 _Unwind_Backtrace）。
// 约束：本进程及其所有依赖都用 -fno-omit-frame-pointer 编译。
// 栈帧布局（x86_64 SysV ABI）:
//   [rbp]     -> 上一帧 rbp
//   [rbp+8]   -> 返回地址 (调用本帧的下一条指令地址)
// 终止条件: rbp 为 0、rbp 不递增、rbp 越界、未对齐、或损坏。
namespace {

// 每线程栈范围缓存。pthread_getattr_np 不能在 hot path 频繁调用。
struct StackBounds { uintptr_t lo = 0; uintptr_t hi = 0; };
thread_local StackBounds t_stack;

void init_stack_bounds() {
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr) != 0) return;
    void* base = nullptr;
    size_t size = 0;
    if (pthread_attr_getstack(&attr, &base, &size) == 0) {
        t_stack.lo = reinterpret_cast<uintptr_t>(base);
        t_stack.hi = t_stack.lo + size;
    }
    pthread_attr_destroy(&attr);
}

inline bool fp_in_stack(uintptr_t fp) {
    if (t_stack.hi == 0) return false;
    // fp 本身至少要能读到 [fp+8]，所以 hi-16 是右界
    return fp >= t_stack.lo && (fp + 16) <= t_stack.hi;
}

}  // namespace

size_t capture_backtrace_fast(int skip, int max_depth,
                              uintptr_t* out, size_t out_cap) {
    if (max_depth <= 0 || out_cap == 0) return 0;
    // 抓栈本身不走劫持
    ScopedBypass _b;

#if defined(__x86_64__) || defined(__aarch64__)
    if (t_stack.hi == 0) init_stack_bounds();
    if (t_stack.hi == 0) {
        // 拿不到栈边界，回退到 glibc backtrace 以保安全
        const int total = skip + max_depth + 2;
        void** buf = static_cast<void**>(alloca(sizeof(void*) * total));
        int n = ::backtrace(buf, total);
        int start = skip < n ? skip : n;
        size_t produced = 0;
        for (int i = start; i < n && produced < out_cap &&
             produced < static_cast<size_t>(max_depth); ++i) {
            out[produced++] = reinterpret_cast<uintptr_t>(buf[i]);
        }
        return produced;
    }

    uintptr_t* fp = static_cast<uintptr_t*>(__builtin_frame_address(0));
    size_t produced = 0;
    int idx = 0;
    const int want = skip + max_depth;
    while (idx < want) {
        uintptr_t fpv = reinterpret_cast<uintptr_t>(fp);
        // 对齐 + 栈范围
        if ((fpv & 7) != 0) break;
        if (!fp_in_stack(fpv)) break;
        uintptr_t* next_fp = reinterpret_cast<uintptr_t*>(fp[0]);
        uintptr_t  ra      = fp[1];
        if (ra == 0) break;
        if (idx >= skip) {
            out[produced++] = ra;
            if (produced >= static_cast<size_t>(max_depth) ||
                produced >= out_cap) break;
        }
        // fp 链必须单调递增（栈往低地址生长，调用者 fp 更高）；否则视为损坏
        if (next_fp <= fp) break;
        fp = next_fp;
        ++idx;
    }
    return produced;
#else
    // 非 x86_64/aarch64 回退到 glibc backtrace
    const int total = skip + max_depth + 2;
    void** buf = static_cast<void**>(alloca(sizeof(void*) * total));
    int n = ::backtrace(buf, total);
    int start = skip < n ? skip : n;
    size_t produced = 0;
    for (int i = start; i < n && produced < out_cap &&
         produced < static_cast<size_t>(max_depth); ++i) {
        out[produced++] = reinterpret_cast<uintptr_t>(buf[i]);
    }
    return produced;
#endif
}

}  // namespace dl
