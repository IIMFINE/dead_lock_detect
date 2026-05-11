// fork 回归：父进程起线程做 lock/unlock，再 fork 一个子进程做 lock/unlock，
// 父进程正常退出。预期：
//   - 父进程 trace 完整（含父侧事件，不含子侧）
//   - 子进程不生成新 trace，且不写入父 trace
//
// 外层 bash 检查：父 trace 行数 > 0；子 trace 不存在或为空。
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>

static pthread_mutex_t g_m = PTHREAD_MUTEX_INITIALIZER;

static void* worker(void*) {
    for (int i = 0; i < 1000; ++i) {
        pthread_mutex_lock(&g_m);
        pthread_mutex_unlock(&g_m);
    }
    return nullptr;
}

int main() {
    pthread_t t;
    pthread_create(&t, nullptr, worker, nullptr);
    pthread_join(t, nullptr);

    pid_t pid = fork();
    if (pid == 0) {
        // 子进程：再做一些锁操作然后退出
        for (int i = 0; i < 1000; ++i) {
            pthread_mutex_lock(&g_m);
            pthread_mutex_unlock(&g_m);
        }
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return 0;
}
