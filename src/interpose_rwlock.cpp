#include "real_symbols.h"
#include "bypass.h"
#include "event_log.h"
#include "event_types.h"
#include "config.h"

#include <pthread.h>
#include <time.h>

using namespace dl;

extern "C" int pthread_rwlock_init(pthread_rwlock_t* rw, const pthread_rwlockattr_t* attr) {
    if (should_bypass()) return real::pthread_rwlock_init(rw, attr);
    ScopedBypass _b;
    int rc = real::pthread_rwlock_init(rw, attr);
    DL_EV(INIT, RWLOCK, rw, rc);
    return rc;
}

extern "C" int pthread_rwlock_destroy(pthread_rwlock_t* rw) {
    if (should_bypass()) return real::pthread_rwlock_destroy(rw);
    ScopedBypass _b;
    DL_EV(DESTROY, RWLOCK, rw, 0);
    return real::pthread_rwlock_destroy(rw);
}

extern "C" int pthread_rwlock_rdlock(pthread_rwlock_t* rw) {
    if (should_bypass()) return real::pthread_rwlock_rdlock(rw);
    ScopedBypass _b;
    int rc = real::pthread_rwlock_rdlock(rw);
    DL_EV(RDLOCK_POST, RWLOCK, rw, rc);
    return rc;
}
extern "C" int pthread_rwlock_tryrdlock(pthread_rwlock_t* rw) {
    if (should_bypass()) return real::pthread_rwlock_tryrdlock(rw);
    ScopedBypass _b;
    int rc = real::pthread_rwlock_tryrdlock(rw);
    DL_EV(TRYRDLOCK_POST, RWLOCK, rw, rc);
    return rc;
}
extern "C" int pthread_rwlock_timedrdlock(pthread_rwlock_t* rw, const struct timespec* abs) {
    if (should_bypass()) return real::pthread_rwlock_timedrdlock(rw, abs);
    ScopedBypass _b;
    int rc = real::pthread_rwlock_timedrdlock(rw, abs);
    DL_EV(TIMEDRDLOCK_POST, RWLOCK, rw, rc);
    return rc;
}
extern "C" int pthread_rwlock_wrlock(pthread_rwlock_t* rw) {
    if (should_bypass()) return real::pthread_rwlock_wrlock(rw);
    ScopedBypass _b;
    int rc = real::pthread_rwlock_wrlock(rw);
    DL_EV(WRLOCK_POST, RWLOCK, rw, rc);
    return rc;
}
extern "C" int pthread_rwlock_trywrlock(pthread_rwlock_t* rw) {
    if (should_bypass()) return real::pthread_rwlock_trywrlock(rw);
    ScopedBypass _b;
    int rc = real::pthread_rwlock_trywrlock(rw);
    DL_EV(TRYWRLOCK_POST, RWLOCK, rw, rc);
    return rc;
}
extern "C" int pthread_rwlock_timedwrlock(pthread_rwlock_t* rw, const struct timespec* abs) {
    if (should_bypass()) return real::pthread_rwlock_timedwrlock(rw, abs);
    ScopedBypass _b;
    int rc = real::pthread_rwlock_timedwrlock(rw, abs);
    DL_EV(TIMEDWRLOCK_POST, RWLOCK, rw, rc);
    return rc;
}

extern "C" int pthread_rwlock_unlock(pthread_rwlock_t* rw) {
    if (should_bypass()) return real::pthread_rwlock_unlock(rw);
    ScopedBypass _b;
    DL_EV(UNLOCK, RWLOCK, rw, 0);
    return real::pthread_rwlock_unlock(rw);
}
