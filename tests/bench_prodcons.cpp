// bench_prodcons —— pthread_mutex lock/unlock 微基准
//
// 设计要点：
// - 用 std::chrono::steady_clock（CLOCK_MONOTONIC，Linux 上走 vDSO）给每次
//   lock+unlock 配对单独计时，结果写进每线程预分配的 vector<uint32_t>，
//   避免分配/IO 进入热路径污染样本。
// - 两种工作模式：
//     uncontended  每线程独立 mutex —— 纯测劫持/链路开销
//     contended    所有线程共享 1 个 mutex —— 端到端含争用
// - 跑完后做 sort + 百分位聚合，stderr 一行打印汇总，方便外层 diff。
//
// 用法：bench_prodcons [uncontended|contended] [threads] [iters_per_thread]
// 默认：uncontended 8 200000

#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct WorkerCtx {
    pthread_mutex_t* mu;       // 指向各自/共享的 mutex
    int iters;
    std::vector<uint32_t> ns;  // 每次 lock+unlock 的纳秒数
};

// pthread_create 用的 thunk，避免 std::thread 在某些 PRELOAD 链上引入 wrapper
void* worker(void* arg) {
    auto* c = static_cast<WorkerCtx*>(arg);
    pthread_mutex_t* m = c->mu;
    uint32_t* out = c->ns.data();
    const int N = c->iters;
    for (int i = 0; i < N; ++i) {
        auto t0 = Clock::now();
        pthread_mutex_lock(m);
        pthread_mutex_unlock(m);
        auto t1 = Clock::now();
        auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        out[i] = (dt > UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(dt);
    }
    return nullptr;
}

double pct(const std::vector<uint32_t>& sorted, double p) {
    if (sorted.empty()) return 0;
    size_t idx = static_cast<size_t>(p * (sorted.size() - 1));
    return static_cast<double>(sorted[idx]);
}

}  // namespace

int main(int argc, char** argv) {
    std::string mode = (argc > 1) ? argv[1] : "uncontended";
    int threads     = (argc > 2) ? std::atoi(argv[2]) : 8;
    int iters       = (argc > 3) ? std::atoi(argv[3]) : 200000;

    if (mode != "uncontended" && mode != "contended") {
        std::fprintf(stderr, "mode must be uncontended|contended\n");
        return 2;
    }

    // 分配 mutex
    std::vector<pthread_mutex_t> mus;
    pthread_mutex_t shared = PTHREAD_MUTEX_INITIALIZER;
    if (mode == "uncontended") {
        mus.assign(threads, PTHREAD_MUTEX_INITIALIZER);
    }

    // 每线程上下文 + 预分配样本缓存（200k * 4B = 800 KB / 线程，不会进 hot path 分配）
    std::vector<WorkerCtx> ctxs(threads);
    for (int i = 0; i < threads; ++i) {
        ctxs[i].mu    = (mode == "uncontended") ? &mus[i] : &shared;
        ctxs[i].iters = iters;
        ctxs[i].ns.resize(iters);
    }

    // 起线程，整体 wall-clock 用同一 monotonic 时钟
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

    // 合并所有样本（uncontended 模式按线程独立计算意义不大，统一聚合即可）
    size_t total = static_cast<size_t>(threads) * static_cast<size_t>(iters);
    std::vector<uint32_t> all;
    all.reserve(total);
    for (auto& c : ctxs) {
        all.insert(all.end(), c.ns.begin(), c.ns.end());
    }
    std::sort(all.begin(), all.end());

    long double sum = 0;
    for (auto v : all) sum += v;
    double avg = static_cast<double>(sum / static_cast<long double>(all.size()));

    std::printf("=== bench_prodcons ===\n");
    std::printf("mode=%s threads=%d iters_per_thread=%d total_ops=%zu\n",
                mode.c_str(), threads, iters, total);
    std::printf("wall_total : %.3f s   ops_per_sec=%.2f M/s\n",
                wall_s, total / wall_s / 1e6);
    std::printf("per-op (ns): avg=%.0f  min=%u  p50=%.0f  p90=%.0f  p99=%.0f  p999=%.0f  max=%u\n",
                avg,
                all.front(),
                pct(all, 0.50),
                pct(all, 0.90),
                pct(all, 0.99),
                pct(all, 0.999),
                all.back());
    return 0;
}
