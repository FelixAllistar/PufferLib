// POSIX pieces the player uses that MinGW's headers lack. Windows builds only:
// tools/build_windows.sh force-includes this file (-include) into every game
// source and links compat.c. Like pthread.h here, it never includes
// <windows.h>, which clashes with raylib.h.
#ifndef CRET_WIN32_COMPAT_H
#define CRET_WIN32_COMPAT_H
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// mkdir(path, mode): Windows has no permission bits.
#define mkdir(path, mode) _mkdir(path)

// clock_gettime and nanosleep (winpthreads is not part of the toolchain).
typedef int clockid_t;
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3
int clock_gettime(clockid_t clock, struct timespec* ts);
int nanosleep(const struct timespec* req, struct timespec* rem);

int setenv(const char* name, const char* value, int overwrite);

// rename() replaces an existing target as on POSIX (the CRT's refuses), so
// the write-temporary-then-rename saves (caches, characters) keep working.
int cret_rename_replace(const char* from, const char* to);
#define rename(from, to) cret_rename_replace(from, to)

#define _SC_NPROCESSORS_ONLN 84
long sysconf(int name);
#endif
#endif
