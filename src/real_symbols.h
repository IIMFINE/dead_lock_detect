#pragma once

#include <pthread.h>
#include <atomic>
#include <time.h>

namespace dl::real {

extern int (*pthread_mutex_init)(pthread_mutex_t*, const pthread_mutexattr_t*);
extern int (*pthread_mutex_destroy)(pthread_mutex_t*);
extern int (*pthread_mutex_lock)(pthread_mutex_t*);
extern int (*pthread_mutex_trylock)(pthread_mutex_t*);
extern int (*pthread_mutex_timedlock)(pthread_mutex_t*, const struct timespec*);
extern int (*pthread_mutex_unlock)(pthread_mutex_t*);

extern int (*pthread_rwlock_init)(pthread_rwlock_t*, const pthread_rwlockattr_t*);
extern int (*pthread_rwlock_destroy)(pthread_rwlock_t*);
extern int (*pthread_rwlock_rdlock)(pthread_rwlock_t*);
extern int (*pthread_rwlock_tryrdlock)(pthread_rwlock_t*);
extern int (*pthread_rwlock_timedrdlock)(pthread_rwlock_t*, const struct timespec*);
extern int (*pthread_rwlock_wrlock)(pthread_rwlock_t*);
extern int (*pthread_rwlock_trywrlock)(pthread_rwlock_t*);
extern int (*pthread_rwlock_timedwrlock)(pthread_rwlock_t*, const struct timespec*);
extern int (*pthread_rwlock_unlock)(pthread_rwlock_t*);

extern int (*pthread_spin_init)(pthread_spinlock_t*, int);
extern int (*pthread_spin_destroy)(pthread_spinlock_t*);
extern int (*pthread_spin_lock)(pthread_spinlock_t*);
extern int (*pthread_spin_trylock)(pthread_spinlock_t*);
extern int (*pthread_spin_unlock)(pthread_spinlock_t*);

extern int (*pthread_cond_wait)(pthread_cond_t*, pthread_mutex_t*);
extern int (*pthread_cond_timedwait)(pthread_cond_t*, pthread_mutex_t*, const struct timespec*);

void init_once() noexcept;
bool ready() noexcept;

}  // namespace dl::real
