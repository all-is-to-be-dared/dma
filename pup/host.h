#pragma once

#include <stdint.h>

enum find_status {
  FIND_OK,
  FIND_NXDEV,
  FIND_NOT_CHR,
  FIND_OPEN_FAIL,
};
enum find_status find_serial_device(const char *s, int *fd, uint32_t baud);




typedef struct {
  const char *self;
  const char *inp_path;
  int inp_fd;
  const char *dev_path;
  int dev_fd;
  uint64_t load_addr;

  bool is_pty;
  bool headless;

  bool _print_usage;
} opts_t;

extern opts_t opts;
