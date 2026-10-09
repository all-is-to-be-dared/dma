#pragma once

#include <inttypes.h>

bool load_elf_image(uintptr_t img_start, uintptr_t img_end, bool elf32_p,
                           bool elfle_p, uint16_t machine, uintptr_t mem_hi,
                           const char *cmdline);

uintptr_t elf_trampoline();


