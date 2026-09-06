#pragma once

#include <stdlib.h>
#include <stdint.h>

struct elf_boot_args {
  void *elf;
  size_t elf_size;
  size_t cmdline_len;
  char cmdline[];
};
