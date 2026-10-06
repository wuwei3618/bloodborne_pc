/* Host platform differences: Linux (the reference) and macOS x86-64 (natively on Intel,
 * under Rosetta 2 on Apple silicon). The Linux branches keep the exact calls the runtime
 * always made; macOS gets equivalents for the glibc and Linux-only interfaces. Include it
 * after the system headers (and _GNU_SOURCE) of the including file. */
#ifndef BB_PLATFORM_H
#define BB_PLATFORM_H
#ifndef _WIN32
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif

/* Assembler names of C symbols: Mach-O prefixes them with an underscore. */
#ifdef __APPLE__
#define BB_ASM_SYMBOL(name) "_" #name
#else
#define BB_ASM_SYMBOL(name) #name
#endif

#ifdef __APPLE__
#define BB_RECURSIVE_MUTEX_INITIALIZER PTHREAD_RECURSIVE_MUTEX_INITIALIZER
#define BB_ST_ATIM(s) ((s)->st_atimespec)
#define BB_ST_MTIM(s) ((s)->st_mtimespec)
#define BB_ST_CTIM(s) ((s)->st_ctimespec)
#define BB_CLOCK_REALTIME_COARSE CLOCK_REALTIME
/* A write to a protected page is SIGBUS on macOS (SIGSEGV is an unmapped address). */
#define BB_ACCESS_FAULT(sig) ((sig) == SIGSEGV || (sig) == SIGBUS)
#define BB_CTX_RIP(uc) ((uc)->uc_mcontext->__ss.__rip)
#define BB_CTX_RBP(uc) ((uc)->uc_mcontext->__ss.__rbp)
#define BB_CTX_RDI(uc) ((uc)->uc_mcontext->__ss.__rdi)
#else
#define BB_RECURSIVE_MUTEX_INITIALIZER PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#define BB_ST_ATIM(s) ((s)->st_atim)
#define BB_ST_MTIM(s) ((s)->st_mtim)
#define BB_ST_CTIM(s) ((s)->st_ctim)
#define BB_CLOCK_REALTIME_COARSE CLOCK_REALTIME_COARSE
#define BB_ACCESS_FAULT(sig) ((sig) == SIGSEGV)
#define BB_CTX_RIP(uc) ((uc)->uc_mcontext.gregs[REG_RIP])
#define BB_CTX_RBP(uc) ((uc)->uc_mcontext.gregs[REG_RBP])
#define BB_CTX_RDI(uc) ((uc)->uc_mcontext.gregs[REG_RDI])
#endif

#ifdef __APPLE__
/* Kernel thread id, for logs and thread dumps. */
static inline uint64_t bb_thread_id(void) {
    uint64_t id=0;
    pthread_threadid_np(NULL,&id);
    return id;
}
/* Names the calling thread. */
static inline void bb_set_thread_name(const char *name) { pthread_setname_np(name); }
/* CPU time of the calling thread (RUSAGE_THREAD): macOS has no per-thread getrusage, so all of
 * it is reported as user time. */
static inline int bb_thread_rusage(struct rusage *usage) {
    memset(usage,0,sizeof(*usage));
    struct timespec t;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)) return -1;
    usage->ru_utime.tv_sec=t.tv_sec; usage->ru_utime.tv_usec=(int)(t.tv_nsec/1000);
    return 0;
}
/* Nanoseconds from now until deadline on clock (far deadlines are capped, past ones <= 0). */
static inline int64_t bb_until(clockid_t clock, const struct timespec *deadline) {
    struct timespec now;
    clock_gettime(clock,&now);
    int64_t seconds=(int64_t)deadline->tv_sec-(int64_t)now.tv_sec;
    if (seconds>1000000000) return INT64_C(1000000000)*1000000000;
    return seconds*1000000000+(deadline->tv_nsec-now.tv_nsec);
}
static inline struct timespec bb_timespec_ns(int64_t ns) {
    struct timespec t={(time_t)(ns/1000000000),(long)(ns%1000000000)};
    return t;
}
#else
#define bb_thread_id() ((uint64_t)gettid())
#define bb_set_thread_name(name) pthread_setname_np(pthread_self(),(name))
#define bb_thread_rusage(usage) getrusage(RUSAGE_THREAD,(usage))
#endif

/* Sleeps until an absolute CLOCK_MONOTONIC time. */
static inline void bb_sleep_until(const struct timespec *deadline) {
#ifdef __APPLE__
    for (int64_t ns; (ns=bb_until(CLOCK_MONOTONIC,deadline))>0;) {
        struct timespec rest=bb_timespec_ns(ns);
        nanosleep(&rest,NULL);
    }
#else
    while (clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,deadline,NULL)) {}
#endif
}

/* Condition variables waited on with absolute CLOCK_MONOTONIC deadlines. macOS has no
 * pthread_condattr_setclock: it waits for the remaining time instead. */
static inline int bb_cond_init_monotonic(pthread_cond_t *cond) {
#ifdef __APPLE__
    return pthread_cond_init(cond,NULL);
#else
    pthread_condattr_t attr;
    int e=pthread_condattr_init(&attr);
    if (e) return e;
    e=pthread_condattr_setclock(&attr,CLOCK_MONOTONIC);
    if (!e) e=pthread_cond_init(cond,&attr);
    int destroyed=pthread_condattr_destroy(&attr);
    return e ? e : destroyed;
#endif
}
static inline int bb_cond_wait_until(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *deadline) {
#ifdef __APPLE__
    int64_t ns=bb_until(CLOCK_MONOTONIC,deadline);
    if (ns<=0) return ETIMEDOUT;
    struct timespec rest=bb_timespec_ns(ns);
    return pthread_cond_timedwait_relative_np(cond,mutex,&rest);
#else
    return pthread_cond_timedwait(cond,mutex,deadline);
#endif
}

#ifdef __APPLE__
/* macOS has no timed mutex or rwlock acquisition: try, then nap, until the CLOCK_REALTIME
 * deadline. Only the guest's timed lock calls get here. */
static inline int bb_poll_lock(int (*attempt)(void *), void *lock, const struct timespec *deadline) {
    for (unsigned tries=0;; ++tries) {
        int e=attempt(lock);
        if (e!=EBUSY) return e;
        if (bb_until(CLOCK_REALTIME,deadline)<=0) return ETIMEDOUT;
        struct timespec nap={0,tries<64 ? 20000 : 500000};
        nanosleep(&nap,NULL);
    }
}
static inline int bb_try_mutex(void *m) { return pthread_mutex_trylock(m); }
static inline int bb_try_read(void *l) { return pthread_rwlock_tryrdlock(l); }
static inline int bb_try_write(void *l) { return pthread_rwlock_trywrlock(l); }
#endif
static inline int bb_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *deadline) {
#ifdef __APPLE__
    return bb_poll_lock(bb_try_mutex,mutex,deadline);
#else
    return pthread_mutex_timedlock(mutex,deadline);
#endif
}
static inline int bb_rwlock_timedlock(pthread_rwlock_t *lock, int writer, const struct timespec *deadline) {
#ifdef __APPLE__
    return bb_poll_lock(writer ? bb_try_write : bb_try_read,lock,deadline);
#else
    return writer ? pthread_rwlock_timedwrlock(lock,deadline) : pthread_rwlock_timedrdlock(lock,deadline);
#endif
}

/* getrandom(2) without flags: the byte count, or -1 with errno. */
static inline long bb_getrandom(void *buffer, size_t size) {
#ifdef __APPLE__
    for (size_t at=0; at<size; at+=256) {
        size_t n=size-at<256 ? size-at : 256; /* getentropy's limit per call */
        if (getentropy((unsigned char *)buffer+at,n)) return -1;
    }
    return (long)size;
#else
    return (long)getrandom(buffer,size,0);
#endif
}
#endif /* !_WIN32 */
#endif
