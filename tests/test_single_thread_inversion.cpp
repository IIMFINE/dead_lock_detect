// 单线程锁顺序反转：同一线程两段代码先后用 (A,B) 和 (B,A) 顺序加锁。
// 这里实际不会死锁，但会产生 A->B 和 B->A 两条边，代表"潜在死锁"
// ——另一个线程若分头执行其中一段就会真死锁。应被检出。
#include <pthread.h>

static pthread_mutex_t A = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t B = PTHREAD_MUTEX_INITIALIZER;

int main() {
    // 段 1: A 先 B 后
    pthread_mutex_lock(&A);
    pthread_mutex_lock(&B);
    pthread_mutex_unlock(&A);
    pthread_mutex_unlock(&B);

    // 段 2: B 先 A 后
    pthread_mutex_lock(&B);
    pthread_mutex_lock(&A);
    pthread_mutex_unlock(&A);
    pthread_mutex_unlock(&B);

    return 0;
}
