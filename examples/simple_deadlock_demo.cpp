#include <pthread.h>
#include <unistd.h>
#include <stdio.h>

pthread_mutex_t mutex_a = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_b = PTHREAD_MUTEX_INITIALIZER;

void* thread_ab(void* arg) {
    printf("Thread AB started\n");
    for (int i = 0; i < 10; i++) {
        pthread_mutex_lock(&mutex_a);
        printf("Thread AB: locked mutex_a\n");

        // 持有 mutex_a 期间睡眠，增加与另一线程冲突的概率
        usleep(200000);  // 200ms

        pthread_mutex_lock(&mutex_b);
        printf("Thread AB: locked mutex_b\n");

        usleep(100000);  // 100ms

        pthread_mutex_unlock(&mutex_b);
        pthread_mutex_unlock(&mutex_a);
        printf("Thread AB: released both\n");

        usleep(100000); // 100ms 间隔
    }
    printf("Thread AB finished\n");
    return nullptr;
}

void* thread_ba(void* arg) {
    printf("Thread BA started\n");

    // 稍微延迟，确保两个线程交错执行
    usleep(50000);

    for (int i = 0; i < 10; i++) {
        pthread_mutex_lock(&mutex_b);
        printf("Thread BA: locked mutex_b\n");

        // 持有 mutex_b 期间睡眠，增加与另一线程冲突的概率
        usleep(200000);  // 200ms

        pthread_mutex_lock(&mutex_a);
        printf("Thread BA: locked mutex_a\n");

        usleep(100000);  // 100ms

        pthread_mutex_unlock(&mutex_a);
        pthread_mutex_unlock(&mutex_b);
        printf("Thread BA: released both\n");

        usleep(100000); // 100ms 间隔
    }
    printf("Thread BA finished\n");
    return nullptr;
}

int main() {
    printf("=== Deadlock Detection Demo ===\n");
    printf("Thread AB: locks A->B\n");
    printf("Thread BA: locks B->A (opposite order)\n");
    printf("This creates a potential deadlock cycle\n\n");

    pthread_t t1, t2;
    pthread_create(&t1, nullptr, thread_ab, nullptr);
    pthread_create(&t2, nullptr, thread_ba, nullptr);

    pthread_join(t1, nullptr);
    pthread_join(t2, nullptr);

    pthread_mutex_destroy(&mutex_a);
    pthread_mutex_destroy(&mutex_b);

    printf("\n=== Program completed successfully ===\n");
    printf("Even though no actual deadlock occurred, the detector\n");
    printf("should identify the potential deadlock pattern.\n");

    return 0;
}
