/* ==========================================================================
 * MOLAN-AV3A — stdio silencing shim (force-included into library TUs)
 * --------------------------------------------------------------------------
 * The upstream reference decoder logs through printf(). To keep the library
 * silent inside a host process, this header is force-included into every
 * library translation unit (MSVC: /FI, GCC/Clang: -include) and shadows
 * printf with a no-op declared below (defined in molan_av3a.c).
 *
 * Why force-include instead of "-Dprintf=molan_av3a_quiet_printf"?
 * A command-line rename is active while <stdio.h> itself is parsed. On
 * MSVC, printf is an *inline function definition* in stdio.h, so the
 * rename produces a second body of molan_av3a_quiet_printf inside every
 * TU (C2084), and call sites end up bound to that inline copy -- which
 * still writes to real stdout, defeating the silencing. Including
 * <stdio.h> here first, then shadowing, keeps every platform consistent:
 * after this header, any printf(...) call resolves to our quiet function.
 *
 * Note for GCC/glibc with _FORTIFY_SOURCE: stdio2.h re-defines printf as
 * __printf_chk(...); the #undef below clears that before shadowing.
 * ==========================================================================
 */
#ifndef MOLAN_AV3A_QUIET_H
#define MOLAN_AV3A_QUIET_H

/* Pull in the real <stdio.h> FIRST, while printf is still untouched. */
#include <stdio.h>
#include <stdarg.h>

#ifdef printf
#undef printf
#endif

int molan_av3a_quiet_printf(const char *fmt, ...);

#define printf molan_av3a_quiet_printf

#endif /* MOLAN_AV3A_QUIET_H */
