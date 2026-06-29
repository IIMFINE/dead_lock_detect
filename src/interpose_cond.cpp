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
    int rc = real::pthread_cond_wait(c, m);
    // POST 带上 mutex 地址：分析器据此知道 wait 期间临时释放过 m，重新获取后才返回
    DL_EV(COND_WAIT_POST, COND, c, (long)reinterpret_cast<uintptr_t>(m));
    return rc;
}

extern "C" int pthread_cond_timedwait(pthread_cond_t* c, pthread_mutex_t* m,
                                      const struct timespec* abs) {
    if (should_bypass()) return real::pthread_cond_timedwait(c, m, abs);
    ScopedBypass _b;
    int rc = real::pthread_cond_timedwait(c, m, abs);
    // 无论 rc 如何都记录：超时返回时 m 也已重新获取
    DL_EV(COND_TIMEDWAIT_POST, COND, c, (long)reinterpret_cast<uintptr_t>(m));
    return rc;
}
