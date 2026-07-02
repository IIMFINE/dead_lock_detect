#include "real_symbols.h"
#include "bypass.h"
#include "event_log.h"
#include "event_types.h"
#include "config.h"
#include "profile.h"

#include <pthread.h>
#include <time.h>

using namespace dl;

extern "C" int pthread_mutex_init(pthread_mutex_t* m, const pthread_mutexattr_t* attr) {
    // 如果 real 函数指针还未初始化，先初始化（处理早期调用）
    if (!real::pthread_mutex_init) {
        real::init_once();
    }
    if (should_bypass()) return real::pthread_mutex_init(m, attr);
    ScopedBypass _b;
    int rc = real::pthread_mutex_init(m, attr);
    long flags = 0;
    if (attr) {
        int type = 0;
        pthread_mutexattr_gettype(attr, &type);
        if (type == PTHREAD_MUTEX_RECURSIVE) flags |= 1;
    }
    DL_EV(INIT, MUTEX, m, (rc == 0) ? flags : rc);
    return rc;
}

extern "C" int pthread_mutex_destroy(pthread_mutex_t* m) {
    if (!real::pthread_mutex_destroy) real::init_once();
    if (should_bypass()) return real::pthread_mutex_destroy(m);
    ScopedBypass _b;
    DL_EV(DESTROY, MUTEX, m, 0);
    return real::pthread_mutex_destroy(m);
}

extern "C" int pthread_mutex_lock(pthread_mutex_t* m) {
    if (!real::pthread_mutex_lock) real::init_once();
    if (should_bypass()) return real::pthread_mutex_lock(m);
    DL_PROFILE_SCOPE("wrap/mutex_lock");
    ScopedBypass _b;
    int rc;
    { DL_PROFILE_SCOPE("wrap/real_mutex_lock"); rc = real::pthread_mutex_lock(m); }
    { DL_PROFILE_SCOPE("wrap/DL_EV(LOCK_POST)"); DL_EV(LOCK_POST, MUTEX, m, rc); }
    return rc;
}

extern "C" int pthread_mutex_trylock(pthread_mutex_t* m) {
    if (!real::pthread_mutex_trylock) real::init_once();
    if (should_bypass()) return real::pthread_mutex_trylock(m);
    ScopedBypass _b;
    int rc = real::pthread_mutex_trylock(m);
    // trylock 无论成功失败都记录：成功 → 进 lock-order 图；失败 → 业务行为信号
    // analyzer 通过 rc 区分（rc==0 才建边）
    DL_EV(TRYLOCK_POST, MUTEX, m, rc);
    return rc;
}

extern "C" int pthread_mutex_timedlock(pthread_mutex_t* m, const struct timespec* abs) {
    if (!real::pthread_mutex_timedlock) real::init_once();
    if (should_bypass()) return real::pthread_mutex_timedlock(m, abs);
    ScopedBypass _b;
    int rc = real::pthread_mutex_timedlock(m, abs);
    // timedlock 同 trylock：超时失败也是有意义的信号
    DL_EV(TIMEDLOCK_POST, MUTEX, m, rc);
    return rc;
}

extern "C" int pthread_mutex_unlock(pthread_mutex_t* m) {
    if (!real::pthread_mutex_unlock) real::init_once();
    if (should_bypass()) return real::pthread_mutex_unlock(m);
    DL_PROFILE_SCOPE("wrap/mutex_unlock");
    ScopedBypass _b;
    { DL_PROFILE_SCOPE("wrap/DL_EV(UNLOCK)"); DL_EV(UNLOCK, MUTEX, m, 0); }
    int rc;
    { DL_PROFILE_SCOPE("wrap/real_mutex_unlock"); rc = real::pthread_mutex_unlock(m); }
    return rc;
}
