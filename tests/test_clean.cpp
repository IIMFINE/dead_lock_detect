// 正常无死锁程序：多线程固定顺序加锁 -> 应完全静默
#include <pthread.h>
#include <unistd.h>

static pthread_mutex_t A = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t B = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t C = PTHREAD_MUTEX_INITIALIZER;

static void* worker(void*) {
    for (int i = 0; i < 100; ++i) {
        pthread_mutex_lock(&A);
        pthread_mutex_lock(&B);
        pthread_mutex_lock(&C);
        pthread_mutex_unlock(&C);
        pthread_mutex_unlock(&B);
        pthread_mutex_unlock(&A);
    }
    return nullptr;
}

int main() {
    pthread_t t[4];
    for (int i = 0; i < 4; ++i) pthread_create(&t[i], nullptr, worker, nullptr);
    for (int i = 0; i < 4; ++i) pthread_join(t[i], nullptr);
    return 0;
}
