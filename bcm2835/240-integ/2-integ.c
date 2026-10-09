#include <bcm2835/arch.h>
#include <bcm2835/mmu.h>
#include <bcm2835/platform.h>
#include <bcm2835/ptags.h>

#include <generic/assert.h>
#include <generic/backtrace.h>
#include <generic/macros.h>
#include <generic/math.h>
#include <generic/printf.h>

#include <string.h>

#include "integ.h"

static _Alignas(0x4000) struct mmu_translation_table ttb = {};

bool
check_coverage(IntegRegistry *ihdr, uintptr_t dram_start, uintptr_t dram_end);

uint32_t
randomize_memory_reference(uint8_t *_Nullable p, size_t s, uint32_t seed);

[[gnu::naked]]
uint32_t
randomize_memory(uint8_t *_Nullable p, size_t s, uint32_t seed);

void
main(struct elf_boot_args *boot_args)
{
  gpio_pin_set_function(14, FSEL_ALT5);
  gpio_pin_set_function(15, FSEL_ALT5);
  aux_uart_init(1152000);
  systmr_delay_ms(1000);
  printf(__FILE__ ": starting\n");
  printf("Boot args:\n");
  printf("\t\x1b[3mName    \tValue\x1b[0m\n");
  printf("\tELF base\t0x%p\n", boot_args->elf);
  printf("\tELF size\t%zu\n", boot_args->elf_size);
  printf("\tCmdline \t<%.*s>\n", boot_args->cmdline_len, boot_args->cmdline);
  printf("\n");
  // if (!backtrace_enable(boot_args->elf))
  //   printf("\n[ \x1b[33mWARNING\x1b[0m: Backtrace functionality not enabled! "
  //          "]\n\n");
  mmu_init(&ttb);
  icache_set_enabled(true);
  brpdx_set_enabled(true);
  dcache_set_enabled(true);
  pmu_enable();

  // -----------------------------------------------------------------------------------------------

  uint8_t test_buffer[1024], ref_buffer[1024];
  uint32_t test_seed_p, ref_seed_p;

  printf("test_buffer@%p ref_buffer@%p\n", test_buffer, ref_buffer);
  
  for(int i = 0;i < 1024;i++) {
    for(int j = 0;j < (1024 - i);j++) {
      printf("i=%d, j=%d\n", i, j);
      memset(test_buffer, 0, 1024);
      memset(ref_buffer, 0, 1024);

      ref_seed_p = randomize_memory_reference(ref_buffer + i, j, 1);
      printf("ref_seed_p = %x\n", ref_seed_p);
      test_seed_p = randomize_memory(test_buffer + i, j, 1);
      printf("test_seed_p = %x\n", test_seed_p);

      assert(ref_seed_p == test_seed_p);
      assert(!memcmp(test_buffer, ref_buffer, j));
    }
  }

  printf("\nDONE.\n");
  aux_uart_flush_tx_fifo();
}

// -------------------------------------------------------------------------------------------------

void
integ_prep()
{
  //
}

// complications:
//  - integrity checking kinda requires stack usage, so we need to do something
//  about that

IntegResult
integ_chk(IntegRegistry *ihdr, const uint8_t *seed)
{
  IntegResult ir;
  size_t i;
  uint32_t hash;

  ir.state = IS_Clean;
  ir.bad_region_idx = 0;
  ir.additional_regions_required = 0;

  for (i = 0; i < ihdr->region_count; i++) {
    hash = memory_hash(seed, ihdr->regions[i].p, ihdr->regions[i].size);
    if (hash != ihdr->regions[i].cksum) {
      if (ihdr->regions[i].flags & IRF_Mutable)
        printf("mutable-marked region changed: #%d\n", i);
      else {
        printf("!!!! non-mutable region changed: #%d\n", i);
        ir.state = max(ir.state, ihdr->regions[i].flags & IRF_Critical ? IS_AbEnd : IS_AbCon);
      }
    }
  }

  return ir;
}

// -------------------------------------------------------------------------------------------------

static inline uint32_t xorshift32(uint32_t *seed) {
  uint32_t t;
  t = *seed;
  t ^= t << 13;
  t ^= t >> 17;
  t ^= t << 5;
  *seed = t;
  return t;
}

uint32_t
randomize_memory_reference(uint8_t *_Nullable p, size_t s, uint32_t seed)
{
  uint8_t *start_word, *stop;
  uint32_t *q, *stop_word;

  stop = p + s;
  stop_word = (uint32_t*)((uintptr_t)stop & ~3U);
  // stop_block = (uint8_t*)((uintptr_t)stop & ~31U);
  // start_block = (uint8_t*)(((uintptr_t)p + 31) & ~31U);
  start_word = (uint8_t*)(((uintptr_t)p + 3) & ~3U);

  // start_block = min(start_block, stop_block);
  start_word = min(start_word, (uint8_t*)stop_word);

  while(p < start_word)
    *p++ = xorshift32(&seed);

  q = (uint32_t *)p;
  while(q < (uint32_t*)stop_word)
    *q++ = xorshift32(&seed);

  p = (uint8_t*)q;
  while(p < stop)
    *p++ = xorshift32(&seed);

  return seed;
}

/// Randomize `s` bytes of memory starting at `p`, using the seed `seed`.
[[gnu::naked]]
uint32_t
randomize_memory(uint8_t *_Nullable p, size_t s, uint32_t seed)
{
  __asm__(
    // ---------------------------------------------------------------------------------------
    // PROLOGUE

    "push {r4-r12, r14}\n"
    "mov r11, r2\n"        // for striping
    "add r14, r0, r1\n"    // r14 = stop

    "1:\n"
    "and r3, r14, #-3\n"   // r3  = stop_word
    "add r12, r0, #3\n"    // r12 = min(start_word, stop_word)
    "and r12, r12, #-3\n"  //       ...
    "cmp r12, r3\n"        //       ...
    "movhi r12, r3\n"      //       ...
    // while r0 < r12
    //   *r0 = xorshift32 ( &r11 ) & 0xff
    //   r0 += 1
    "cmp r0, r12\n"
    "bhs 3f\n"
    "2:\n"
    "eor r11, r11, r11, lsl #13\n"
    "eor r11, r11, r11, lsr #17\n"
    "eor r11, r11, r11, lsl #5\n"
    "strb r11, [r0], #1\n"
    "cmp r0, r12\n"
    "bne 2b\n"

    "3:\n"

    "and r3, r14, #-31\n"  // r3  = stop_block
    "add r12, r0, #31\n"   // r12 = min(start_block, stop_block)
    "and r12, r12, #-31\n" //       ...
    "cmp r12, r3\n"        //       ...
    "movhi r12, r3\n"      //       ...
    // while r0 < r12
    //   *r0 = xorshift32 ( &r11 )
    //   r0 += 4
    "cmp r0, r12\n"
    "bhs 10f\n"
    "4:\n"
    "eor r11, r11, r11, lsl #13\n"
    "eor r11, r11, r11, lsr #17\n"
    "eor r11, r11, r11, lsl #5\n"
    "str r11, [r0], #4\n"
    "cmp r0, r12\n"
    "bne 4b\n"

    // ---------------------------------------------------------------------------------------
    // 32-WIDE STRIPED BLOCKS

    "10:\n"
    // while r0 < r3
    //   *r0[0:8] = xorshift32.x8 ( &r11 )
    //   r0 += 32
    "cmp r0, r3\n"
    "beq 20f\n"

    "11:\n"
    "eor r4, r11, r11, lsl #13\n"
    "eor r5, r4, r4, lsl #13\n"
    "eor r6, r5, r5, lsl #13\n"
    "eor r7, r6, r6, lsl #13\n"
    "eor r8, r7, r7, lsl #13\n"
    "eor r9, r8, r8, lsl #13\n"
    "eor r10, r9, r9, lsl #13\n"
    "eor r11, r10, r10, lsl #13\n"
    "eor r4, r4, r4, lsr #17\n"
    "eor r5, r5, r5, lsr #17\n"
    "eor r6, r6, r6, lsr #17\n"
    "eor r7, r7, r7, lsr #17\n"
    "eor r8, r8, r8, lsr #17\n"
    "eor r9, r9, r9, lsr #17\n"
    "eor r10, r10, r10, lsr #17\n"
    "eor r11, r11, r11, lsr #17\n"
    "eor r4, r4, r4, lsl #5\n"
    "eor r5, r5, r5, lsl #5\n"
    "eor r6, r6, r6, lsl #5\n"
    "eor r7, r7, r7, lsl #5\n"
    "eor r8, r8, r8, lsl #5\n"
    "eor r9, r9, r9, lsl #5\n"
    "eor r10, r10, r10, lsl #5\n"
    "eor r11, r11, r11, lsl #5\n"

    "stmia r0!, {r4-r11}\n"
    "cmp r0, r3\n"
    "bne 11b\n"

    // ---------------------------------------------------------------------------------------
    // TAIL
    "20:\n"

    "and r3, r14, #-3\n" // r3  = stop_word

    // while r0 != r3
    //   *r0 = xorshift32( &r11 )
    //   r0 += 4
    "cmp r0, r3\n"
    "bne 22f\n"
    "21:\n"
    "eor r11, r11, r11, lsl #13\n"
    "eor r11, r11, r11, lsr #17\n"
    "eor r11, r11, r11, lsl #5\n"
    "str r11, [r0], #4\n"
    "cmp r0, r3\n"
    "bne 21b\n"

    // while r0 != r14
    //   *r0 xorshift32( &r11 ) & 0xff
    //   r0 += 1
    "22:\n"
    "cmp r0, r14\n"
    "bne 30f\n"
    "23:\n"
    "eor r11, r11, r11, lsl #13\n"
    "eor r11, r11, r11, lsr #17\n"
    "eor r11, r11, r11, lsr #5\n"
    "strb r11, [r0], #1\n"
    "cmp r0, r14\n"
    "bne 23b\n"

    // ---------------------------------------------------------------------------------------
    // EPILOGUE
    "30:\n"
    "pop {r4-r12, r14}\n"
    "mov r0, r11\n"
    "bx lr\n" ::: "memory");
}

// -------------------------------------------------------------------------------------------------

typedef struct
{
  uintptr_t start, end;
} range;

static inline bool
range_overlap(range lhs, range rhs)
{
  return lhs.start <= rhs.end && rhs.start <= lhs.end;
}

static inline range
region_range(IntegRegion region)
{
  range region_range;

  region_range.start = (uintptr_t)region.p;
  region_range.end = region_range.start + region.size;

  return region_range;
}

static inline void
region_swap(IntegRegion *restrict r1, IntegRegion *restrict r2)
{
  IntegRegion tmp;
  memcpy(&tmp, r1, sizeof tmp);
  memcpy(r1, r2, sizeof tmp);
  memcpy(r2, &tmp, sizeof tmp);
}

/// Check if an IntegHeader covers all of memory
bool
check_regions_coverage(IntegRegistry *ihdr, uintptr_t dram_start, uintptr_t dram_end)
{
  size_t i, j, k, size;
  void *p, *q;
  range header_range;

  header_range.start = (uintptr_t)ihdr;
  header_range.end = (uintptr_t)(ihdr->regions + ihdr->region_count);

  for (i = 0; i < ihdr->region_count; i++)
    if (range_overlap(region_range(ihdr->regions[i]), header_range)) {
      printf("ERROR: headers overlap with region #%zu!\n", i);
      return false;
    }

  // sort by region start
  for (i = 0; i < ihdr->region_count; i++) {
    k = 0;
    p = ihdr->regions[i].p;
    for (j = i + 1; j < ihdr->region_count; j++)
      if (ihdr->regions[j].p < p) {
        p = ihdr->regions[j].p;
        k = j;
      }
    if (k)
      region_swap(ihdr->regions + i, ihdr->regions + j);
  }

  q = (void *)dram_start;
  j = 0;
  // check for region overlap, ignoring zero-sized regions
  for (i = 0; i < ihdr->region_count; i++) {
    p = ihdr->regions[i].p;
    if (q > p && (uintptr_t)q != dram_start) {
      printf("ERROR: regions overlap: #%d and #%d\n", j, i);
      return false;
    } else if (q < p) {
      if (q == ihdr && p == ihdr->regions + ihdr->region_count)
        // case: gap is the IntegHeader
        continue;
      printf("ERROR: regions have gap: #%d and #%d\n", j, i);
      return false;
    }
    size = ihdr->regions[i].size;
    if (!size)
      continue;
    q = p + size;
    j = i;
  }
  if ((uintptr_t)q != dram_end) {
    printf("ERROR: final region has gap with end of DRAM\n");
    return false;
  }

  return true;
}
