// 异步管道回归：多线程大量 lock/unlock，然后主进程正常退出。
// 外层 bash 断言 trace 里 LOCK_PRE / LOCK_POST 行数均 == N * ITERS。
#include <pthread.h>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr int kThreads = 8;
constexpr int kIters   = 100000;

pthread_mutex_t g_m = PTHREAD_MUTEX_INITIALIZER;

void* worker(void*) {
    for (int i = 0; i < kIters; ++i) {
        pthread_mutex_lock(&g_m);
        pthread_mutex_unlock(&g_m);
    }
    return nullptr;
}

}  // namespace

int main() {
    pthread_t t[kThreads];
    for (int i = 0; i < kThreads; ++i) pthread_create(&t[i], nullptr, worker, nullptr);
    for (int i = 0; i < kThreads; ++i) pthread_join(t[i], nullptr);
    return 0;
}
