// RECURSIVE mutex 同线程三次加锁 -> 不应报告环
#include <pthread.h>

int main() {
    pthread_mutex_t m;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m, &attr);
    pthread_mutexattr_destroy(&attr);

    pthread_mutex_lock(&m);
    pthread_mutex_lock(&m);
    pthread_mutex_lock(&m);
    pthread_mutex_unlock(&m);
    pthread_mutex_unlock(&m);
    pthread_mutex_unlock(&m);

    pthread_mutex_destroy(&m);
    return 0;
}
