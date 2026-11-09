// Win32 implementations of compat.h and pthread.h (Windows builds only).
#ifdef _WIN32
#include "compat.h"
#include "pthread.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <process.h>
#include <windows.h>

_Static_assert(sizeof(SRWLOCK) == sizeof(void*), "pthread_mutex_t holds an SRWLOCK");

// ---------------------------------------------------------------- threads
struct CretThread { HANDLE h; void* (*fn)(void*); void* arg; void* ret; };

static unsigned __stdcall thread_main(void* p) {
    struct CretThread* t = p;
    t->ret = t->fn(t->arg);
    return 0;
}
int pthread_create(pthread_t* out, const pthread_attr_t* attr, void* (*fn)(void*), void* arg) {
    struct CretThread* t = calloc(1, sizeof *t);
    if (!t) return ENOMEM;
    t->fn = fn; t->arg = arg;
    unsigned stack = attr && attr->stack_size > 0 ? (unsigned)attr->stack_size : 8u << 20;
    // Reserve (not commit) the stack, as Linux does; pages commit as they are touched.
    t->h = (HANDLE)_beginthreadex(NULL, stack, thread_main, t, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (!t->h) { free(t); return EAGAIN; }
    *out = t;
    return 0;
}
int pthread_join(pthread_t t, void** ret) {
    if (!t) return EINVAL;
    WaitForSingleObject(t->h, INFINITE);
    CloseHandle(t->h);
    if (ret) *ret = t->ret;
    free(t);
    return 0;
}
int pthread_mutex_init(pthread_mutex_t* m, const pthread_mutexattr_t* attr) { (void)attr; InitializeSRWLock((SRWLOCK*)&m->srw); return 0; }
int pthread_mutex_destroy(pthread_mutex_t* m) { (void)m; return 0; }
int pthread_mutex_lock(pthread_mutex_t* m) { AcquireSRWLockExclusive((SRWLOCK*)&m->srw); return 0; }
int pthread_mutex_unlock(pthread_mutex_t* m) { ReleaseSRWLockExclusive((SRWLOCK*)&m->srw); return 0; }

// ---------------------------------------------------------------- clocks
static void from_100ns(uint64_t t, struct timespec* ts) { ts->tv_sec = (time_t)(t / 10000000u); ts->tv_nsec = (long)(t % 10000000u) * 100; }
int clock_gettime(clockid_t clock, struct timespec* ts) {
    if (clock == CLOCK_MONOTONIC) {
        static LARGE_INTEGER freq; LARGE_INTEGER c;
        if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&c);
        ts->tv_sec = (time_t)(c.QuadPart / freq.QuadPart);
        ts->tv_nsec = (long)((c.QuadPart % freq.QuadPart) * 1000000000LL / freq.QuadPart);
        return 0;
    }
    if (clock == CLOCK_REALTIME) {
        FILETIME f; GetSystemTimePreciseAsFileTime(&f);
        uint64_t t = ((uint64_t)f.dwHighDateTime << 32 | f.dwLowDateTime) - 116444736000000000ull;   // since 1970
        from_100ns(t, ts);
        return 0;
    }
    if (clock == CLOCK_PROCESS_CPUTIME_ID || clock == CLOCK_THREAD_CPUTIME_ID) {
        FILETIME c, e, k, u;
        BOOL ok = clock == CLOCK_PROCESS_CPUTIME_ID ? GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)
                                                    : GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u);
        if (!ok) { errno = EINVAL; return -1; }
        uint64_t t = ((uint64_t)k.dwHighDateTime << 32 | k.dwLowDateTime) + ((uint64_t)u.dwHighDateTime << 32 | u.dwLowDateTime);
        from_100ns(t, ts);
        return 0;
    }
    errno = EINVAL;
    return -1;
}

// A high-resolution waitable timer (Windows 10 1803+) sleeps for about the
// time asked; Sleep() rounds up to the 1-16 ms scheduler tick.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
int nanosleep(const struct timespec* req, struct timespec* rem) {
    if (rem) rem->tv_sec = 0, rem->tv_nsec = 0;
    int64_t ns = (int64_t)req->tv_sec * 1000000000LL + req->tv_nsec;
    if (ns <= 0) return 0;
    static _Thread_local HANDLE timer; static _Thread_local int tried;
    if (!tried) { tried = 1; timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS); }
    if (timer) {
        LARGE_INTEGER due; due.QuadPart = -(ns + 99) / 100;   // relative, 100 ns units
        if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) { WaitForSingleObject(timer, INFINITE); return 0; }
    }
    Sleep((DWORD)((ns + 999999) / 1000000));
    return 0;
}

// ---------------------------------------------------------------- misc
int setenv(const char* name, const char* value, int overwrite) {
    if (!overwrite && getenv(name)) return 0;
    return _putenv_s(name, value) == 0 ? 0 : -1;
}
int cret_rename_replace(const char* from, const char* to) {
    if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) return 0;
    errno = GetLastError() == ERROR_FILE_NOT_FOUND ? ENOENT : EACCES;
    return -1;
}
long sysconf(int name) {
    if (name != _SC_NPROCESSORS_ONLN) { errno = EINVAL; return -1; }
    SYSTEM_INFO si; GetSystemInfo(&si);
    return (long)si.dwNumberOfProcessors;
}
#endif
