#include "real_symbols.h"
#include "bypass.h"
#include "event_log.h"
#include "event_types.h"
#include "config.h"

#include <pthread.h>

using namespace dl;

static const void* strip_v(pthread_spinlock_t* s) {
    return const_cast<const void*>(reinterpret_cast<volatile const void*>(s));
}

extern "C" int pthread_spin_init(pthread_spinlock_t* s, int pshared) {
    if (should_bypass()) return real::pthread_spin_init(s, pshared);
    ScopedBypass _b;
    int rc = real::pthread_spin_init(s, pshared);
    DL_EV(INIT, SPIN, strip_v(s), rc);
    return rc;
}

extern "C" int pthread_spin_destroy(pthread_spinlock_t* s) {
    if (should_bypass()) return real::pthread_spin_destroy(s);
    ScopedBypass _b;
    DL_EV(DESTROY, SPIN, strip_v(s), 0);
    return real::pthread_spin_destroy(s);
}

extern "C" int pthread_spin_lock(pthread_spinlock_t* s) {
    if (should_bypass()) return real::pthread_spin_lock(s);
    ScopedBypass _b;
    DL_EV(LOCK_PRE, SPIN, strip_v(s), 0);
    int rc = real::pthread_spin_lock(s);
    DL_EV(LOCK_POST, SPIN, strip_v(s), rc);
    return rc;
}
extern "C" int pthread_spin_trylock(pthread_spinlock_t* s) {
    if (should_bypass()) return real::pthread_spin_trylock(s);
    ScopedBypass _b;
    DL_EV(TRYLOCK_PRE, SPIN, strip_v(s), 0);
    int rc = real::pthread_spin_trylock(s);
    DL_EV(TRYLOCK_POST, SPIN, strip_v(s), rc);
    return rc;
}

extern "C" int pthread_spin_unlock(pthread_spinlock_t* s) {
    if (should_bypass()) return real::pthread_spin_unlock(s);
    ScopedBypass _b;
    DL_EV(UNLOCK, SPIN, strip_v(s), 0);
    return real::pthread_spin_unlock(s);
}
