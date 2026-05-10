// 三线程 A->B, B->C, C->A；每个线程先持一锁再请求下一锁（用 trylock 避免真卡死）
#include <pthread.h>
#include <unistd.h>
#include <atomic>

static pthread_mutex_t A = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t B = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t C = PTHREAD_MUTEX_INITIALIZER;
static std::atomic<int> ready{0};

static void body(pthread_mutex_t* first, pthread_mutex_t* second) {
    pthread_mutex_lock(first);
    ready.fetch_add(1);
    while (ready.load() < 3) usleep(100);
    if (pthread_mutex_trylock(second) == 0) pthread_mutex_unlock(second);
    pthread_mutex_unlock(first);
}

static void* t1(void*) { body(&A, &B); return nullptr; }
static void* t2(void*) { body(&B, &C); return nullptr; }
static void* t3(void*) { body(&C, &A); return nullptr; }

int main() {
    pthread_t a, b, c;
    pthread_create(&a, nullptr, t1, nullptr);
    pthread_create(&b, nullptr, t2, nullptr);
    pthread_create(&c, nullptr, t3, nullptr);
    pthread_join(a, nullptr);
    pthread_join(b, nullptr);
    pthread_join(c, nullptr);
    return 0;
}
