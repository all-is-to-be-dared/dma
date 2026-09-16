#pragma once

#include <inttypes.h>

#if __ARM_ARCH == 6

typedef union {
  struct {
    uintptr_t pc, fp;
  };
} backtrace_cursor;

#else
#error "Unsupported architecture for backtrace functionality"
#endif




bool backtrace_enable(void *elf_image_base);

bool backtrace_enabled(void);

/// Construct a new backtrace cursor.
backtrace_cursor backtrace_cursor_new(void);

/// Get information about the current call frame pointed to by the cursor.
///
/// If `pc` is non-null, then it will be written with the address at which the currently pointed-to
/// call frame is paused.
///
/// If `sym` is non-null, then it will be written with a pointer to a character string giving the
/// name of the pointed-to call frame, if a name can be found. If one cannot, then it will be
/// written with a null pointer.
void backtrace_cursor_read(backtrace_cursor *curs,
                           uintptr_t *pc,
                           const char **sym);

/// Step cursor one call up the stack. Returns false if there's no further calls.
bool backtrace_cursor_next(backtrace_cursor *curs);
