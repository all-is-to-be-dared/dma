#pragma once

#include <stdint.h>

static inline void
dsb(void)
{
  __asm__ volatile("mcr p15, 0, %0, c7, c10, 4" ::"r"(0u));
}

static inline void
flush_dcache(void)
{
  // TODO
}

static inline uint32_t
cycle_count_read(void)
{
  uint32_t out;
  __asm__ volatile ("mrc p15, 0, %0, c15, c12, 1" : "=r"(out));
  return out;
}

static inline void
cycle_count_write(uint32_t v)
{
  __asm__ volatile ("mcr p15, 0, %0, c15, c12, 1" :: "r"(v));
}

static inline void
pmu_enable(void)
{
  __asm__ volatile ("mcr p15, 0, %0, c15, c12, 0" :: "r"(1));
}
