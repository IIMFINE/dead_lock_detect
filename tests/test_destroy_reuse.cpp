// destroy 后地址复用：第一阶段 A->B 的边属于 (addr_A, gen=1)；
// destroy A 后同地址新建为 gen=2 的锁 C，再 C->B（真实是"正向 C->B"），
// 之后又 B->C（反向），如果 gen 没生效，会错误地把 A 与 C 的边合并成环。
// 此测试用单线程顺序操作；不会产生任何真正死锁，应完全静默。
#include <pthread.h>
#include <cstring>

int main() {
    // 用一块栈空间放锁
    char buf[sizeof(pthread_mutex_t) * 2];
    auto* pA = reinterpret_cast<pthread_mutex_t*>(buf);
    auto* pB = reinterpret_cast<pthread_mutex_t*>(buf + sizeof(pthread_mutex_t));

    pthread_mutex_init(pA, nullptr);
    pthread_mutex_init(pB, nullptr);
    pthread_mutex_lock(pA);
    pthread_mutex_lock(pB);  // 边 A->B
    pthread_mutex_unlock(pB);
    pthread_mutex_unlock(pA);
    pthread_mutex_destroy(pA);

    // 同地址复用为新锁 C
    memset(pA, 0, sizeof(pthread_mutex_t));
    pthread_mutex_init(pA, nullptr);
    pthread_mutex_lock(pB);
    pthread_mutex_lock(pA);  // 边 B->C（pA 此时 gen 不同于之前的 A）
    pthread_mutex_unlock(pA);
    pthread_mutex_unlock(pB);

    pthread_mutex_destroy(pA);
    pthread_mutex_destroy(pB);
    return 0;
}
