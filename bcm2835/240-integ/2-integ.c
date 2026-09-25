#include <bcm2835/arch.h>
#include <bcm2835/mmu.h>
#include <bcm2835/platform.h>
#include <bcm2835/ptags.h>

#include <generic/printf.h>
#include <generic/assert.h>
#include <generic/backtrace.h>
#include <generic/macros.h>
#include <generic/math.h>

#include <string.h>

#include "integ.h"

static _Alignas(0x4000) struct mmu_translation_table ttb = {};

bool
check_coverage(IntegRegistry *ihdr, uintptr_t dram_start, uintptr_t dram_end);

void
main(struct elf_boot_args *boot_args)
{
  gpio_pin_set_function(14, FSEL_ALT5);
  gpio_pin_set_function(15, FSEL_ALT5);
  aux_uart_init(11520000, 400'000'000);
  systmr_delay_ms(1000);
  printf(__FILE__ ": starting\n");
  printf("Boot args:\n");
  printf("\t\x1b[3mName    \tValue\x1b[0m\n");
  printf("\tELF base\t0x%p\n", boot_args->elf);
  printf("\tELF size\t%zu\n", boot_args->elf_size);
  printf("\tCmdline \t<%.*s>\n", boot_args->cmdline_len, boot_args->cmdline);
  printf("\n");
  if (!backtrace_enable(boot_args->elf))
    printf("\n[ \x1b[33mWARNING\x1b[0m: Backtrace functionality not enabled! ]\n\n");
  mmu_init(&ttb);
  icache_set_enabled(true);
  brpdx_set_enabled(true);
  dcache_set_enabled(true);
  pmu_enable();

  // -----------------------------------------------------------------------------------------------
}

// -------------------------------------------------------------------------------------------------



void
integ_prep()
{
  //
}

// complications:
//  - integrity checking kinda requires stack usage, so we need to do something about that

IntegResult
integ_chk(IntegRegistry *ihdr, const uint8_t *seed)
{
  IntegResult ir;
  size_t i;
  uint32_t hash;

  ir.state = IS_Clean;
  ir.bad_region_idx = 0;
  ir.additional_regions_required = 0;

  for(i = 0;i < ihdr->region_count;i++) {
    hash = memory_hash(seed, ihdr->regions[i].p, ihdr->regions[i].size);
    if(hash != ihdr->regions[i].cksum) {
      if(ihdr->regions[i].flags & IRF_Mutable)
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

[[gnu::naked]]
void
purgable_prep(void *_Nullable p, size_t s, uint32_t seed)
{
  // splitmix32
  // a += 0x9e3779b9
  // t = a
  // t ^= (a >> 16) * 0x21f0aaad
  // t ^= (t >> 15) * 0x735a2d97
  // t ^= (t >> 15)
  // return t
  __asm__(
    "adr r3, 2f\n"
    "stm r3, {r4-r14}\n"
    "ldr r4, =0x9e3779b9\n"
    "ldr r5, =0x735a2d96\n"

    // TODO: need to add checks against s

    "tst r0, #31\n"
    "beq 5f\n"

    "tst r0, #3\n"
    "beq 4f\n"

    // pre.x1:
    "add r2, r2, r4\n"
    "mov r6, r2, lsr #16\n"
    "mul r6, r6, r4\n"
    "lsr r6, r6, #15\n"
    "mul r6, r6, r5\n"
    "lsr r6, r6, #15\n"
    "3:\n"
    "strb r6, [r0], #1\n"
    "lsr r6, r6, #8\n"
    "tst r0, #3\n"
    "bne 3b\n"

    // pre.x4
    "4:\n"
    "add r2, r2, r4\n"
    "mov r6, r2, lsr #16\n"
    "mul r6, r6, r4\n"
    "lsr r6, r6, #15\n"
    "mul r6, r6, r5\n"
    "lsr r6, r6, #15\n"
    "str r6, [r0], #4\n"
    "tst r0, #31\n"
    "bne 4b\n"

    // hot
    "5:\n"
    "stmia r0!, {r6-r13}\n"
    ""

    // post.x4
    "5:\n"

    // post.x1
    "6:\n"
    
    "add r3, r3, r4\n"
    "adr r3, 2f\n"
    "ldm r3, {r4-r14}\n"
    "bx lr\n"
    //
    "2: .rept 11\n.word 0\n.endr\n"
    ::: "memory"
  );
}

// -------------------------------------------------------------------------------------------------

typedef struct
{
  uintptr_t start, end;
} range;

static inline bool
range_overlap(
  range lhs,
  range rhs
)
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
  
  for(i = 0;i < ihdr->region_count;i++)
    if(range_overlap(region_range(ihdr->regions[i]), header_range)) {
      printf("ERROR: headers overlap with region #%zu!\n", i);
      return false;
    }

  // sort by region start
  for(i = 0;i < ihdr->region_count;i++) {
    k = 0;
    p = ihdr->regions[i].p;
    for(j = i+1;j < ihdr->region_count;j++)
      if(ihdr->regions[j].p < p) {
        p = ihdr->regions[j].p;
        k = j;
      }
    if(k)
      region_swap(ihdr->regions + i, ihdr->regions + j);
  }

  q = (void*)dram_start;
  j = 0;
  // check for region overlap, ignoring zero-sized regions
  for(i = 0;i < ihdr->region_count;i++) {
    p = ihdr->regions[i].p;
    if(q > p && (uintptr_t)q != dram_start) {
      printf("ERROR: regions overlap: #%d and #%d\n", j, i);
      return false;
    } else if (q < p) {
      if(q == ihdr && p == ihdr->regions + ihdr->region_count)
        // case: gap is the IntegHeader
        continue;
      printf("ERROR: regions have gap: #%d and #%d\n", j, i);
      return false;
    }
    size = ihdr->regions[i].size;
    if(!size)
      continue;
    q = p + size;
    j = i;
  }
  if((uintptr_t)q != dram_end) {
    printf("ERROR: final region has gap with end of DRAM\n");
    return false;
  }

  return true;
}
