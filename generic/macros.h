#pragma once

/* __CAPTURED_CFLAGS is defined in rules.ninja. We do this here mostly so the LSP doesn't protest, and as a
   backup.
 */
#if !defined(__CAPTURED_CFLAGS)
#define __CAPTURED_CFLAGS ""
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define __GCC__ 1
#endif

/* Prevent the expression `ex` from being optimized away.
 */
#define BLACK_BOX(ex)                     \
  ({                                       \
    __typeof__(ex) t = ex, *s = &t;       \
    asm volatile("" : "+m"(s)::"memory"); \
    t;                                    \
  })
