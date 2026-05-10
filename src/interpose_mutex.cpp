#include "real_symbols.h"
#include "bypass.h"
#include "event_log.h"
#include "event_types.h"
#include "config.h"

#include <pthread.h>
#include <time.h>

using namespace dl;

extern "C" int pthread_mutex_init(pthread_mutex_t* m, const pthread_mutexattr_t* attr) {
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
    if (should_bypass()) return real::pthread_mutex_destroy(m);
    ScopedBypass _b;
    DL_EV(DESTROY, MUTEX, m, 0);
    return real::pthread_mutex_destroy(m);
}

extern "C" int pthread_mutex_lock(pthread_mutex_t* m) {
    if (should_bypass()) return real::pthread_mutex_lock(m);
    ScopedBypass _b;
    DL_EV(LOCK_PRE, MUTEX, m, 0);
    int rc = real::pthread_mutex_lock(m);
    DL_EV(LOCK_POST, MUTEX, m, rc);
    return rc;
}

extern "C" int pthread_mutex_trylock(pthread_mutex_t* m) {
    if (should_bypass()) return real::pthread_mutex_trylock(m);
    ScopedBypass _b;
    DL_EV(TRYLOCK_PRE, MUTEX, m, 0);
    int rc = real::pthread_mutex_trylock(m);
    DL_EV(TRYLOCK_POST, MUTEX, m, rc);
    return rc;
}

extern "C" int pthread_mutex_timedlock(pthread_mutex_t* m, const struct timespec* abs) {
    if (should_bypass()) return real::pthread_mutex_timedlock(m, abs);
    ScopedBypass _b;
    DL_EV(TIMEDLOCK_PRE, MUTEX, m, 0);
    int rc = real::pthread_mutex_timedlock(m, abs);
    DL_EV(TIMEDLOCK_POST, MUTEX, m, rc);
    return rc;
}

extern "C" int pthread_mutex_unlock(pthread_mutex_t* m) {
    if (should_bypass()) return real::pthread_mutex_unlock(m);
    ScopedBypass _b;
    DL_EV(UNLOCK, MUTEX, m, 0);
    return real::pthread_mutex_unlock(m);
}
