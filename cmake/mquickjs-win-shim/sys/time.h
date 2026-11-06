/* <sys/time.h> for MicroQuickJS's host tool on Windows (cmake/mquickjs.cmake): MSVC's C runtime has none, and mqjs.c,
 * upstream's REPL and compiler, reads the time of day through it (Date.now, performance.now and the random seed). This
 * is the part it uses, gettimeofday over the system clock; the engine itself (mquickjs.c) does not include it. */
#ifndef RP_MQUICKJS_WIN_SYS_TIME_H
#define RP_MQUICKJS_WIN_SYS_TIME_H

#include <winsock2.h> /* struct timeval; before windows.h, which readline_tty.c includes after this */
#include <windows.h>

static int gettimeofday(struct timeval *tv, void *tz)
{
    FILETIME ft;
    ULARGE_INTEGER t;
    (void)tz;
    GetSystemTimeAsFileTime(&ft);
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    /* 100 ns ticks since 1601-01-01 to microseconds since 1970-01-01 */
    t.QuadPart = (t.QuadPart - 116444736000000000ULL) / 10;
    tv->tv_sec = (long)(t.QuadPart / 1000000);
    tv->tv_usec = (long)(t.QuadPart % 1000000);
    return 0;
}

#endif
