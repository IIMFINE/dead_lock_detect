// 经典 producer/consumer：1 mutex + 1 cond，两线程对称等待/通知 -> 不应报告环
#include <pthread.h>
#include <unistd.h>
#include <queue>

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  c = PTHREAD_COND_INITIALIZER;
static std::queue<int> q;
static bool done = false;

static void* producer(void*) {
    for (int i = 0; i < 5; ++i) {
        pthread_mutex_lock(&m);
        q.push(i);
        pthread_cond_signal(&c);
        pthread_mutex_unlock(&m);
        usleep(1000);
    }
    pthread_mutex_lock(&m);
    done = true;
    pthread_cond_broadcast(&c);
    pthread_mutex_unlock(&m);
    return nullptr;
}

static void* consumer(void*) {
    while (true) {
        pthread_mutex_lock(&m);
        while (q.empty() && !done) pthread_cond_wait(&c, &m);
        if (q.empty() && done) {
            pthread_mutex_unlock(&m);
            break;
        }
        q.pop();
        pthread_mutex_unlock(&m);
    }
    return nullptr;
}

int main() {
    pthread_t p, cs;
    pthread_create(&p, nullptr, producer, nullptr);
    pthread_create(&cs, nullptr, consumer, nullptr);
    pthread_join(p, nullptr);
    pthread_join(cs, nullptr);
    return 0;
}
