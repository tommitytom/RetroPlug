/*
 * Windows (clang-cl) compatibility shim for the SameBoy Core.
 *
 * Force-included ahead of SameBoy's own headers on the `sameboy` target (via
 * /FI, see cmake/sameboy.cmake). SameBoy's Core is written for POSIX + GNU
 * toolchains; this bridges the few gaps under the MSVC UCRT/STL without
 * modifying the pristine deps/sameboy submodule. Each fix is independent:
 */

#pragma once

/* apu.c uses M_PI. The UCRT's <math.h> only defines it when _USE_MATH_DEFINES
 * is set before the first inclusion — do that here, then pull <math.h> in so
 * SameBoy's later include is a guarded no-op with M_PI already available. */
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <math.h>

/* SameBoy's defs.h defines `noinline` as a GNU attribute (under GB_INTERNAL),
 * which collides with the UCRT's `__declspec(noinline)` in <stdio.h>
 * (corecrt_stdio_config.h). Parse that header first so its include guard is set
 * before defs.h redefines the keyword. */
#include <stdio.h>

/* gb.c calls alloca(), which on Windows is declared in <malloc.h> (not
 * <stdlib.h> as on POSIX). Without this it is implicitly declared int-returning
 * and clang-cl errors on the int-to-pointer assignment. */
#include <malloc.h>

/* save_state.c uses the POSIX type ssize_t. Map it to the Win32 signed size
 * type. */
#include <basetsd.h>
#if !defined(_SSIZE_T_DEFINED) && !defined(ssize_t)
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED
#endif

/* gb.c's GB_attributed_logv uses the GNU extension vasprintf(), absent on the
 * MSVC CRT. Provide a portable implementation (sized via _vscprintf). */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
static int sameboy_vasprintf(char **strp, const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int len = _vscprintf(fmt, ap2);
    va_end(ap2);
    if (len < 0) { *strp = NULL; return -1; }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { *strp = NULL; return -1; }
    int r = vsnprintf(buf, (size_t)len + 1, fmt, ap);
    *strp = buf;
    return r;
}
#define vasprintf sameboy_vasprintf
