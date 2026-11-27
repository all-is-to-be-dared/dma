#pragma once

#include "printf/printf.h" // IWYU pragma: export

#ifdef __cplusplus
extern "C" {
#endif

#if __ARM_ARCH == 6
#include "../bcm2835/platform.h"
#endif

#undef printf
#undef vprintf

static inline int vprintf(const char *s, va_list va)
{
  int r;
  r = vprintf_(s, va);
#if __ARM_ARCH == 6
  aux_uart_flush_tx_fifo();
#endif
  return r;
}

[[gnu::format(printf, 1, 2)]]
static inline int printf(const char *s, ...)
{
  va_list args;
  int r;
  
  va_start(args, s);
  r = vprintf(s, args);
  va_end(args);
  return r;
}

#ifdef __cplusplus
}
#endif
