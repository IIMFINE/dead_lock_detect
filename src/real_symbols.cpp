#include "real_symbols.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

namespace dl::real {

int (*pthread_mutex_init)(pthread_mutex_t*, const pthread_mutexattr_t*) = nullptr;
int (*pthread_mutex_destroy)(pthread_mutex_t*) = nullptr;
int (*pthread_mutex_lock)(pthread_mutex_t*) = nullptr;
int (*pthread_mutex_trylock)(pthread_mutex_t*) = nullptr;
int (*pthread_mutex_timedlock)(pthread_mutex_t*, const struct timespec*) = nullptr;
int (*pthread_mutex_unlock)(pthread_mutex_t*) = nullptr;

int (*pthread_rwlock_init)(pthread_rwlock_t*, const pthread_rwlockattr_t*) = nullptr;
int (*pthread_rwlock_destroy)(pthread_rwlock_t*) = nullptr;
int (*pthread_rwlock_rdlock)(pthread_rwlock_t*) = nullptr;
int (*pthread_rwlock_tryrdlock)(pthread_rwlock_t*) = nullptr;
int (*pthread_rwlock_timedrdlock)(pthread_rwlock_t*, const struct timespec*) = nullptr;
int (*pthread_rwlock_wrlock)(pthread_rwlock_t*) = nullptr;
int (*pthread_rwlock_trywrlock)(pthread_rwlock_t*) = nullptr;
int (*pthread_rwlock_timedwrlock)(pthread_rwlock_t*, const struct timespec*) = nullptr;
int (*pthread_rwlock_unlock)(pthread_rwlock_t*) = nullptr;

int (*pthread_spin_init)(pthread_spinlock_t*, int) = nullptr;
int (*pthread_spin_destroy)(pthread_spinlock_t*) = nullptr;
int (*pthread_spin_lock)(pthread_spinlock_t*) = nullptr;
int (*pthread_spin_trylock)(pthread_spinlock_t*) = nullptr;
int (*pthread_spin_unlock)(pthread_spinlock_t*) = nullptr;

int (*pthread_cond_wait)(pthread_cond_t*, pthread_mutex_t*) = nullptr;
int (*pthread_cond_timedwait)(pthread_cond_t*, pthread_mutex_t*, const struct timespec*) = nullptr;

static std::atomic<bool> g_ready{false};

static void* resolve(const char* name) {
    void* p = dlsym(RTLD_NEXT, name);
    if (!p) {
        fprintf(stderr, "[deadlock] dlsym(%s) failed: %s\n", name, dlerror());
        abort();
    }
    return p;
}

static void* resolve_versioned(const char* name, const char* ver) {
    void* p = dlvsym(RTLD_NEXT, name, ver);
    if (!p) p = dlsym(RTLD_NEXT, name);
    if (!p) {
        fprintf(stderr, "[deadlock] dl(v)sym(%s) failed: %s\n", name, dlerror());
        abort();
    }
    return p;
}

#define R(name)     (decltype(name))resolve(#name)
#define RV(name, v) (decltype(name))resolve_versioned(#name, v)

void init_once() noexcept {
    static std::atomic<int> state{0};
    int expected = 0;
    if (!state.compare_exchange_strong(expected, 1)) {
        while (!g_ready.load(std::memory_order_acquire)) { /* spin */ }
        return;
    }

    pthread_mutex_init     = R(pthread_mutex_init);
    pthread_mutex_destroy  = R(pthread_mutex_destroy);
    pthread_mutex_lock     = R(pthread_mutex_lock);
    pthread_mutex_trylock  = R(pthread_mutex_trylock);
    pthread_mutex_timedlock= R(pthread_mutex_timedlock);
    pthread_mutex_unlock   = R(pthread_mutex_unlock);

    pthread_rwlock_init        = R(pthread_rwlock_init);
    pthread_rwlock_destroy     = R(pthread_rwlock_destroy);
    pthread_rwlock_rdlock      = R(pthread_rwlock_rdlock);
    pthread_rwlock_tryrdlock   = R(pthread_rwlock_tryrdlock);
    pthread_rwlock_timedrdlock = R(pthread_rwlock_timedrdlock);
    pthread_rwlock_wrlock      = R(pthread_rwlock_wrlock);
    pthread_rwlock_trywrlock   = R(pthread_rwlock_trywrlock);
    pthread_rwlock_timedwrlock = R(pthread_rwlock_timedwrlock);
    pthread_rwlock_unlock      = R(pthread_rwlock_unlock);

    pthread_spin_init    = R(pthread_spin_init);
    pthread_spin_destroy = R(pthread_spin_destroy);
    pthread_spin_lock    = R(pthread_spin_lock);
    pthread_spin_trylock = R(pthread_spin_trylock);
    pthread_spin_unlock  = R(pthread_spin_unlock);

    // glibc 对 cond 做过结构体升级，默认符号名解析到 @GLIBC_2.3.2 的旧版会段错。
    // 强制取 GLIBC_2.3.2 之后的新版（若存在），失败再回退默认。
    pthread_cond_wait      = RV(pthread_cond_wait,      "GLIBC_2.3.2");
    pthread_cond_timedwait = RV(pthread_cond_timedwait, "GLIBC_2.3.2");

    g_ready.store(true, std::memory_order_release);
}

bool ready() noexcept {
    return g_ready.load(std::memory_order_acquire);
}

}  // namespace dl::real
