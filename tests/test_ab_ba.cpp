// 两线程 AB-BA：应被检出。
// 劫持点在 pre_acquire 入图——不论真实 lock 是否成功，只要两侧请求动作都发生了，
// 依赖图就会形成 A->B 和 B->A，程序正常退出后 atexit 报告环。
#include <pthread.h>
#include <unistd.h>
#include <atomic>

static pthread_mutex_t A = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t B = PTHREAD_MUTEX_INITIALIZER;
static std::atomic<int> phase{0};

extern "C" void* th1_ab(void*) {
    pthread_mutex_lock(&A);
    phase.fetch_add(1);
    while (phase.load() < 2) usleep(100);
    if (pthread_mutex_trylock(&B) == 0) {
        pthread_mutex_unlock(&B);
    }
    pthread_mutex_unlock(&A);
    return nullptr;
}

extern "C" void* th2_ab(void*) {
    pthread_mutex_lock(&B);
    phase.fetch_add(1);
    while (phase.load() < 2) usleep(100);
    if (pthread_mutex_trylock(&A) == 0) {
        pthread_mutex_unlock(&A);
    }
    pthread_mutex_unlock(&B);
    return nullptr;
}

int main() {
    pthread_t t1, t2;
    pthread_create(&t1, nullptr, th1_ab, nullptr);
    pthread_create(&t2, nullptr, th2_ab, nullptr);
    pthread_join(t1, nullptr);
    pthread_join(t2, nullptr);
    return 0;
}
