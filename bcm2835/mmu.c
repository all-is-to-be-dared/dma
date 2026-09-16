#include "bcm2835/arch.h"
#include "bcm2835/platform.h"
#include <stddef.h>

#include <bcm2835/mmu.h>

#include <generic/printf.h>

#ifndef __clang__
#define _BitInt(x) int
#endif

static inline uint32_t
supersection_entry(uint8_t idx,
                   unsigned _BitInt(4) domain,
                   unsigned _BitInt(3) tex,
                   unsigned _BitInt(1) c,
                   unsigned _BitInt(1) b)
{
  return 0b10                        // Supersection
         | (idx << 24)               //
         | (0 << 19)                 // NS = Stay in secure world
         | (1 << 18)                 // 16 MiB supersection
         | (0 << 17)                 // nG = Global
         | (0 << 16)                 // S = Non-Shared memory
         | (0 << 15)                 // APX = Privileged mode can RW
         | (((uint32_t)tex) << 12)   // Set OS bits
         | (3 << 10)                 // AP = User mode can RW
         | (0 << 9)                  // P = No ECC
         | (((uint32_t)domain) << 5) // Set domain (4 bits)
         | (0 << 4)                  // XN = Allow W|X
         | (((uint32_t)c) << 3)      // Set ?
         | (((uint32_t)b) << 2)      // Set ?
    ;
}

#define cp15_define(name, pNN, op1, crn, crm, op2)                                  \
  [[maybe_unused]] static inline void name##_write(uint32_t x)                      \
  {                                                                                 \
    __asm__ volatile("mcr " #pNN "," #op1 ",%0," #crn "," #crm "," #op2::"r"(x));   \
  }                                                                                 \
  [[maybe_unused]] static inline uint32_t name##_read(void)                         \
  {                                                                                 \
    uint32_t t;                                                                     \
    __asm__ volatile("mrc " #pNN "," #op1 ",%0," #crn "," #crm "," #op2 : "=r"(t)); \
    return t;                                                                       \
  }


cp15_define(control_register, p15, 0, c1, c0, 0);
// Per-process page table.
cp15_define(translation_table_base_register_0, p15, 0, c2, c0, 0);
// OS page table.
cp15_define(translation_table_base_register_1, p15, 0, c2, c1, 0);
cp15_define(translation_table_base_control_register, p15, 0, c2, c0, 2);
cp15_define(domain_access_control_register, p15, 0, c3, c0, 0);

#define cp15_cache_op(crm, op2, x) __asm__ volatile("mcr p15,0,%0,c7," #crm "," #op2::"r"(x))

/// NOTE: Also flushes BTC and BTAC.
[[maybe_unused]]
static inline void
invalidate_entire_icache(void)
{
  __asm__ volatile("mcr p15,0,%0,c7,c5,0\n"
                   "mcr p15,0,%0,c7,c5,0\n"
                   "mcr p15,0,%0,c7,c5,0\n"
                   "mcr p15,0,%0,c7,c5,0\n"
                   ".rept 11\n"
                   "nop\n"
                   ".endr\n" ::"r"(0));
}
[[maybe_unused]]
static inline void
prefetch_flush(void)
{
  cp15_cache_op(c5, 4, 0);
}
[[maybe_unused]]
static inline void
invalidate_entire_btc(void)
{
  cp15_cache_op(c5, 6, 0);
}
[[maybe_unused]]
static inline void
invalidate_entire_dcache(void)
{
  cp15_cache_op(c6, 0, 0);
}
[[maybe_unused]]
static inline void
invalidate_both_caches(void)
{
  // XXX: Seems to be broken
  // cp15_cache_op(c7, 0, 0);

  invalidate_entire_icache();
  invalidate_entire_dcache();
}
[[maybe_unused]]
static inline void
flush_entire_dcache(void)
{
  cp15_cache_op(c14, 0, 0);
}

#define cp15_tlb_op(crm, op2, x) __asm__ volatile("mcr p15,0,%0,c8," #crm "," #op2 ::"r"(x))

[[maybe_unused]]
static inline void
invalidate_tlb(void)
{
  cp15_tlb_op(c7, 0, 0);
}

enum cache_attributes
{
  CA_Noncacheable = 0b00,
  CA_WriteBackWriteAllocate = 0b01,
  CA_WriteThroughNoAllocateOnWrite = 0b10,
  CA_WriteBackNoAllocateOnWrite = 0b11,
};

void
mmu_init(struct mmu_translation_table *tbl)
{
  size_t i;

  // -----------------------------------------------------------------------------------------------
  // Initialize translation table

  for (i = 0; i < 4096; i++)
    tbl->entries[i] = 0;
  for (i = 0; i < 512; i++)
    // cached normal memory, outer writethrough NWA, inner writeback WA
    tbl->entries[i] = supersection_entry(i >> 4, 0, 0b001, 0, 1);
  for (i = 512; i < 512 + 16; i++)
    // shared device memory
    tbl->entries[i] = supersection_entry(i >> 4, 0, 0b000, 0, 1);

  // -----------------------------------------------------------------------------------------------
  // MMU reset

  invalidate_both_caches();
  dsb();
  invalidate_tlb();
  dsb();
  prefetch_flush();

  // -----------------------------------------------------------------------------------------------
  // Set up MMU parameters

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbitwise-op-parentheses"

  // Set up domains
  domain_access_control_register_write(0b00'01'11);

  // Set table pointer in translation table base register (0)
  translation_table_base_register_0_write(
    ((uintptr_t)tbl->entries)          // Translation table base address
    | (CA_WriteBackWriteAllocate << 3) // Outer cacheable attributes
    | (0 << 2)                         // P=0 ECC = disabled
    | (0 << 1)                         // S=0 Page table walk is: Non-Shared
    | (1 << 0)                         // C=1 Page table walk is: Inner cacheable
  );

  // Update translation table control register
  translation_table_base_control_register_write(
    (1 << 5)       // PD1=1 No page table walk on TTBR1
    | (0 << 4)     // PD0=0 _Yes_ page table walk on TTBR0
    | (0b000 << 0) // N=0b000 Boundary size of TTBR0 is 16KiB
  );
  // IMPORTANT: ARM1176 cannot walk from L1d! Need to flush before walking.

  control_register_write(control_register_read() | (1 << 23) // XP1=1 Enable ARMv6 page tables
                         | (1 << 28)                         // TR=1  Enable TEX remap functionality
  );

  control_register_write(control_register_read() & ~(1 << 12) // disable icache
  );


  // icache_set_enabled(false);
  // brpdx_set_enabled(false);

  dsb(); // maybe unnecessary?
  prefetch_flush();

  invalidate_entire_icache();
  dsb();

  control_register_write(control_register_read() | (1 << 0) // MMU enable
                         | (1 << 1)                         // L1d% enable
  );
  prefetch_flush();
  invalidate_entire_btc();
  prefetch_flush();

#pragma GCC diagnostic pop
}
