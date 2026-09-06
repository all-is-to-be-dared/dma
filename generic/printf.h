#pragma once

#include "printf/printf.h" // IWYU pragma: export

#undef printf
#undef vprintf

static inline int vprintf(const char *s, va_list va)
{
  return vprintf_(s, va);
}

[[gnu::format(printf, 1, 2)]]
static inline int printf(const char *s, ...)
{
  va_list args;
  int r;
  
  va_start(args);
  r = vprintf_(s, args);
  va_end(args);
  return r;
}
