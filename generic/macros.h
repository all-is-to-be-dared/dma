#pragma once

/* _CFLAGS_ is defined in rules.ninja. We do this here mostly so the LSP doesn't protest, and as a
   backup.
 */
#if !defined(_CFLAGS_)
#define _CFLAGS_ ""
#endif

/* Prevent the expression `ex` from being optimized away.
 */
#define BLACK_BOX(ex)                     \
  ({                                       \
    __typeof__(ex) t = ex, *s = &t;       \
    asm volatile("" : "+m"(s)::"memory"); \
    t;                                    \
  })
