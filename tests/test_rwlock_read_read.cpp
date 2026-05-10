// rwlock 读-读反向顺序 -> 默认不报（读-读边过滤）
#include <pthread.h>
#include <unistd.h>
#include <atomic>

static pthread_rwlock_t A = PTHREAD_RWLOCK_INITIALIZER;
static pthread_rwlock_t B = PTHREAD_RWLOCK_INITIALIZER;
static std::atomic<int> ready{0};

static void* t1(void*) {
    pthread_rwlock_rdlock(&A);
    ready.fetch_add(1);
    while (ready.load() < 2) usleep(100);
    pthread_rwlock_rdlock(&B);
    pthread_rwlock_unlock(&B);
    pthread_rwlock_unlock(&A);
    return nullptr;
}
static void* t2(void*) {
    pthread_rwlock_rdlock(&B);
    ready.fetch_add(1);
    while (ready.load() < 2) usleep(100);
    pthread_rwlock_rdlock(&A);
    pthread_rwlock_unlock(&A);
    pthread_rwlock_unlock(&B);
    return nullptr;
}

int main() {
    pthread_t a, b;
    pthread_create(&a, nullptr, t1, nullptr);
    pthread_create(&b, nullptr, t2, nullptr);
    pthread_join(a, nullptr);
    pthread_join(b, nullptr);
    return 0;
}
