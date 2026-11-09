// Minimal pthreads for the native Windows player (tools/build_windows.sh puts
// this directory on the include path; Linux never sees it). The game uses only
// create/join and plain mutexes. Implemented over Win32 threads and SRW locks
// in compat.c, so this header does not pull in <windows.h> (which clashes with
// raylib.h: CloseWindow, ShowCursor, Rectangle, ...).
#ifndef CRET_WIN32_PTHREAD_H
#define CRET_WIN32_PTHREAD_H
#ifndef _WIN32
#error "ocean/cretaceous/win32/pthread.h is for Windows builds only"
#endif

typedef struct CretThread* pthread_t;
typedef struct { int stack_size; } pthread_attr_t;
typedef struct { void* srw; } pthread_mutex_t;   // an SRWLOCK (one pointer; zero is unlocked)
typedef struct { int unused; } pthread_mutexattr_t;
#define PTHREAD_MUTEX_INITIALIZER {0}

// Threads get an 8 MiB stack, the Linux default (Windows' own is 1 MiB).
int pthread_create(pthread_t* t, const pthread_attr_t* attr, void* (*fn)(void*), void* arg);
int pthread_join(pthread_t t, void** ret);
int pthread_mutex_init(pthread_mutex_t* m, const pthread_mutexattr_t* attr);
int pthread_mutex_destroy(pthread_mutex_t* m);
int pthread_mutex_lock(pthread_mutex_t* m);
int pthread_mutex_unlock(pthread_mutex_t* m);
#endif
