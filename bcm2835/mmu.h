#pragma once

#include <inttypes.h>

struct
[[gnu::aligned(4096*4)]]
mmu_translation_table
{
  uint32_t entries[4096];
};

void mmu_init(struct mmu_translation_table *);
