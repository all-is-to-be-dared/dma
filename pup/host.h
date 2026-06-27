#pragma once

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

enum find_status
{
  FIND_OK,
  FIND_NXDEV,
  FIND_NOT_CHR,
  FIND_OPEN_FAIL,
};
enum find_status
find_serial_device(const char* s, int* fd, uint32_t baud);




enum dbg_lev
{
  DBG_NONE = 0,
  DBG_MIN = 1,
  DBG_FULL = 2,
};




typedef struct
{
  const char* self;
  const char* inp_path;
  int inp_fd;
  const char* dev_path;
  int dev_fd;
  uint64_t load_addr;

  bool is_pty;
  bool headless;
  uint32_t con_baud;
  enum dbg_lev debug;

  uint8_t retries;

  bool _print_usage;
} opts_t;

extern opts_t opts;




#define host_printf(lev, ...) do { if(opts.debug >= lev) __host_printf(__VA_ARGS__); } while (0)

static inline void __host_printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}
