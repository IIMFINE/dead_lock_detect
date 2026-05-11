#include "thread_state.h"
#include "bypass.h"
#include "config.h"
#include "ring_buffer.h"
#include "backend.h"

#include <new>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace dl {

static thread_local uint64_t t_tid = 0;
static thread_local ThreadRing* t_ring = nullptr;

static pthread_key_t g_ring_key;
static pthread_once_t g_ring_key_once = PTHREAD_ONCE_INIT;

static void ring_tls_destructor(void* arg) {
    // 线程退出：标记 ring 的生产者已死，由 backend 回收
    ThreadRing* r = static_cast<ThreadRing*>(arg);
    if (r) backend_mark_ring_dead(r);
}

static void init_ring_key() {
    pthread_key_create(&g_ring_key, &ring_tls_destructor);
}

uint64_t current_tid() noexcept {
    if (t_tid == 0) {
        t_tid = static_cast<uint64_t>(syscall(SYS_gettid));
    }
    return t_tid;
}

ThreadRing* current_ring() noexcept {
    if (t_ring) return t_ring;
    ScopedBypass _bp;
    pthread_once(&g_ring_key_once, &init_ring_key);
    size_t cap = static_cast<size_t>(config().ring_bytes);
    t_ring = new (std::nothrow) ThreadRing(cap);
    if (!t_ring) return nullptr;
    if (!t_ring->ok()) {
        delete t_ring;
        t_ring = nullptr;
        return nullptr;
    }
    pthread_setspecific(g_ring_key, t_ring);
    backend_register_ring(t_ring);
    return t_ring;
}

void reset_thread_state_for_fork() {
    t_tid = 0;
    // 子进程里 backend_fork_child_reset() 已把所有 ring delete 了，这里只置空指针
    t_ring = nullptr;
}

}  // namespace dl