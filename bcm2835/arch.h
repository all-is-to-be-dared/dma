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
pmu_set_enabled(bool enabled)
{
  __asm__ volatile ("mcr p15, 0, %0, c15, c12, 0" :: "r"(enabled ? 1 : 0));
}

static inline void
pmu_enable(void)
{
  pmu_set_enabled(true);
}

static inline void
icache_set_enabled(bool enabled)
{
  register uint32_t t;
  __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(t));
  if(enabled)
    t |= (1 << 12);
  else
    t &= ~(1 << 12);
  __asm__ volatile ("mcr p15, 0, %0, c1, c0, 0" :: "r"(t));
}

static inline void
brpdx_set_enabled(bool enabled)
{
  register uint32_t t;
  __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(t));
  if(enabled)
    t |= (1 << 11);
  else
    t &= ~(1 << 11);
  __asm__ volatile ("mcr p15, 0, %0, c1, c0, 0" :: "r"(t));
}

static inline void
dcache_set_enabled(bool enabled)
{
  register uint32_t t;
  __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(t));
  if(enabled)
    t |= (1 << 2);
  else
    t &= ~(1 << 2);
  __asm__ volatile ("mcr p15, 0, %0, c1, c0, 0" :: "r"(t));
}

#define cp15_define(name, pNN, op1, crn, crm, op2)                             \
  [[maybe_unused]] static inline void name##_write(uint32_t x) {               \
    __asm__ volatile("mcr " #pNN "," #op1 ",%0," #crn "," #crm                 \
                     "," #op2::"r"(x));                                        \
  }                                                                            \
  [[maybe_unused]] static inline uint32_t name##_read(void) {                  \
    uint32_t t;                                                                \
    __asm__ volatile("mrc " #pNN "," #op1 ",%0," #crn "," #crm "," #op2        \
                     : "=r"(t));                                               \
    return t;                                                                  \
  }

cp15_define(control_register, p15, 0, c1, c0, 0);
// Per-process page table.
cp15_define(translation_table_base_register_0, p15, 0, c2, c0, 0);
// OS page table.
cp15_define(translation_table_base_register_1, p15, 0, c2, c1, 0);
cp15_define(translation_table_base_control_register, p15, 0, c2, c0, 2);
cp15_define(domain_access_control_register, p15, 0, c3, c0, 0);

#define cp15_cache_op(crm, op2, x)                                             \
  __asm__ volatile("mcr p15,0,%0,c7," #crm "," #op2::"r"(x))

/// NOTE: Also flushes BTC and BTAC.
[[maybe_unused]]
static inline void invalidate_entire_icache(void) {
  __asm__ volatile("mcr p15,0,%0,c7,c5,0\n"
                   "mcr p15,0,%0,c7,c5,0\n"
                   "mcr p15,0,%0,c7,c5,0\n"
                   "mcr p15,0,%0,c7,c5,0\n"
                   ".rept 11\n"
                   "nop\n"
                   ".endr\n" ::"r"(0));
}
[[maybe_unused]]
static inline void prefetch_flush(void) {
  cp15_cache_op(c5, 4, 0);
}
[[maybe_unused]]
static inline void invalidate_entire_btc(void) {
  cp15_cache_op(c5, 6, 0);
}
[[maybe_unused]]
static inline void invalidate_entire_dcache(void) {
  cp15_cache_op(c6, 0, 0);
}
[[maybe_unused]]
static inline void invalidate_both_caches(void) {
  // XXX: Seems to be broken
  // cp15_cache_op(c7, 0, 0);

  invalidate_entire_icache();
  invalidate_entire_dcache();
}
[[maybe_unused]]
static inline void flush_entire_dcache(void) {
  // Clean and invalidate entire data cache
  cp15_cache_op(c14, 0, 0);
}

#define cp15_tlb_op(crm, op2, x)                                               \
  __asm__ volatile("mcr p15,0,%0,c8," #crm "," #op2 ::"r"(x))

[[maybe_unused]]
static inline void invalidate_tlb(void) {
  cp15_tlb_op(c7, 0, 0);
}

