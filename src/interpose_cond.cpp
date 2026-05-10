#include "real_symbols.h"
#include "bypass.h"
#include "event_log.h"
#include "event_types.h"
#include "config.h"

#include <pthread.h>
#include <time.h>

using namespace dl;

extern "C" int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m) {
    if (should_bypass()) return real::pthread_cond_wait(c, m);
    ScopedBypass _b;
    // m 的地址随事件写入 rc_or_flags 槽，便于离线分析识别 wait 期间释放的是哪把锁
    DL_EV(COND_WAIT_PRE, COND, c, (long)reinterpret_cast<uintptr_t>(m));
    int rc = real::pthread_cond_wait(c, m);
    DL_EV(COND_WAIT_POST, COND, c, rc);
    return rc;
}

extern "C" int pthread_cond_timedwait(pthread_cond_t* c, pthread_mutex_t* m,
                                      const struct timespec* abs) {
    if (should_bypass()) return real::pthread_cond_timedwait(c, m, abs);
    ScopedBypass _b;
    DL_EV(COND_TIMEDWAIT_PRE, COND, c, (long)reinterpret_cast<uintptr_t>(m));
    int rc = real::pthread_cond_timedwait(c, m, abs);
    DL_EV(COND_TIMEDWAIT_POST, COND, c, rc);
    return rc;
}
