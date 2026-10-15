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
integ_check_coverage(IntegHeader *ihdr, uintptr_t dram_start, uintptr_t dram_end);

IntegResult
integ_run(IntegHeader *ihdr, void (*f)(void *), void *arg);

// uint32_t
// randomize_memory_reference(uint8_t *_Nullable p, size_t s, uint32_t seed);

[[gnu::naked]]
uint32_t
randomize_memory(uint8_t *_Nullable p, size_t s, uint32_t seed);

void
test_ok(void *arg)
{
  assert(arg == (void *)1);
  printf("running test_ok\n");
  return;
}

void
test_corrupt_code_section(void *arg)
{
  printf("running %s\n", __func__);
  *(volatile uint8_t *)0x8000 = 1;
}

void
test_corrupt_first_void(void *arg)
{
  printf("running %s\n", __func__);
  *(volatile uint8_t *)0 = 1;
}

void
test_corrupt_stack(void *arg)
{
  printf("running %s\n", __func__);
  *(volatile uint8_t *)arg = 1;
}

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

  // uint8_t test_buffer[1024], ref_buffer[1024];
  // uint32_t test_seed_p, ref_seed_p;
  // printf("test_buffer@%p ref_buffer@%p\n", test_buffer, ref_buffer);
  // for (int i = 0; i < 1024; i++) {
  //   for (int j = 0; j < (1024 - i); j++) {
  //     printf("i=%d, j=%d\n", i, j);
  //     memset(test_buffer, 0, 1024);
  //     memset(ref_buffer, 0, 1024);

  //     ref_seed_p = randomize_memory_reference(ref_buffer + i, j, 1);
  //     printf("ref_seed_p = %x\n", ref_seed_p);
  //     test_seed_p = randomize_memory(test_buffer + i, j, 1);
  //     printf("test_seed_p = %x\n", test_seed_p);

  //     assert(ref_seed_p == test_seed_p);
  //     assert(!memcmp(test_buffer, ref_buffer, j));
  //   }
  // }

  struct ptag_mem_region arm_mem;
  arm_mem = ptag_get_arm_memory();

  printf("ARM memory: %p - %p\n", (void *)arm_mem.base, (void *)arm_mem.base + arm_mem.size);

  enum
  {
    MAX_REGIONS = 32
  };
  [[gnu::section(".integ")]] static uint8_t alignas(alignof(IntegHeader))
    ihdr_[MICROSTACK_SIZE + sizeof(IntegHeader) + MAX_REGIONS * sizeof(IntegRegion)];
  IntegHeader *ihdr = (IntegHeader *)(ihdr_ + MICROSTACK_SIZE);
  uint8_t SEED[SEED_SIZE];
  for (int i = 0; i < SEED_SIZE; i++)
    SEED[i] = i;

  memset(ihdr_, 0, MICROSTACK_SIZE);
  ihdr->self_hash = 0;
  ihdr->lr = 0;
  ihdr->f = NULL;
  ihdr->f_arg = NULL;
  ihdr->seed = SEED;
  ihdr->flags = 0; // IHF_ExpectStackCleanup
  ihdr->stack.sp = 0;
  ihdr->region_count = 0;
  ihdr->region_cap = MAX_REGIONS;

  extern uint8_t __exec_start[], __exec_end[], __rodata_start[], __rodata_end[], __data_start[],
    __data_end[], __bss_start[], __bss_end[], __stack_start[], __stack_end[], __prog_end[];

  ihdr->regions[0] = (IntegRegion){
    .p = 0,
    .size = (size_t)__exec_start,
    .flags = IRF_Purgable,
    .cksum = 0xDEAD9999,
  };
  ihdr->regions[1] = (IntegRegion){
    .p = __exec_start,
    .size = __exec_end - __exec_start,
    // .size = 0xe10,
    .flags = IRF_Critical,
    .cksum = 0xDEADAAAA,
  };
  ihdr->regions[2] = (IntegRegion){
    .p = __rodata_start,
    .size = __rodata_end - __rodata_start,
    .flags = IRF_Critical,
    .cksum = 0xDEADBBBB,
  };
  ihdr->regions[3] = (IntegRegion){
    .p = __data_start,
    .size = __bss_end - __data_start,
    .flags = IRF_Critical,
    .cksum = 0xDEADCCCC,
  };
  ihdr->regions[4] = (IntegRegion){
    .p = __stack_start,
    .size = __stack_end - __stack_start,
    .flags = IRF_Stack,
    .cksum = 0xDEADDDDD,
  };
  ihdr->stack.stack_lo = (uintptr_t)__stack_start;
  ihdr->regions[5] = (IntegRegion){
    .p = __prog_end,
    .size = arm_mem.base + arm_mem.size - (uintptr_t)__prog_end,
    .flags = IRF_Purgable,
    .cksum = 0xDEADEEEE,
  };
  ihdr->region_count = 6;

  uint32_t k;

  k = 0;

  integ_check_coverage(ihdr, arm_mem.base, arm_mem.base + arm_mem.size);

  integ_run(ihdr, test_ok, (void *)1);

  for(int i = 0;i < ihdr->region_count;i++)
    ihdr->regions[i].flags &= ~(IRF_Corrupted | IRF_CorruptedSomewhere);
  integ_run(ihdr, test_corrupt_first_void, NULL);

  for(int i = 0;i < ihdr->region_count;i++)
    ihdr->regions[i].flags &= ~(IRF_Corrupted | IRF_CorruptedSomewhere);
  integ_run(ihdr, test_corrupt_code_section, NULL);

  for(int i = 0;i < ihdr->region_count;i++)
    ihdr->regions[i].flags &= ~(IRF_Corrupted | IRF_CorruptedSomewhere);
  integ_run(ihdr, test_corrupt_stack, &k);

  printf("\nDONE.\n");
  aux_uart_flush_tx_fifo();
}

// -------------------------------------------------------------------------------------------------

typedef struct
{
  uintptr_t start, end;
} range;

static inline bool
range_overlap(range lhs, range rhs)
{
  // [a,b) and [c,d)
  //
  // if a <= d && c < b
  return lhs.start < rhs.end && rhs.start < lhs.end;
}

static inline bool
range_contains(range reg, uintptr_t addr)
{
  return reg.start <= addr && addr < reg.end;
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
integ_check_coverage(IntegHeader *ihdr, uintptr_t dram_start, uintptr_t dram_end)
{
  size_t i, j, k, size;
  void *p, *q;
  range header_range, reg_range, other_reg_range;

  header_range.start = (uintptr_t)ihdr;
  header_range.end = (uintptr_t)(ihdr->regions + ihdr->region_cap);

  printf("header_range: [%p,%p)\n", (void *)header_range.start, (void *)header_range.end);
  extern uint8_t __integ_start[], __integ_end[];
  printf(".integ range: [%p,%p)\n", __integ_start, __integ_end);

  for (i = 0; i < ihdr->region_count; i++) {
    reg_range = region_range(ihdr->regions[i]);
    if (range_overlap(reg_range, header_range)) {
      printf("ERROR: headers (%p,%p) overlap with region #%zu (%p, %p)!\n",
             (void *)header_range.start,
             (void *)header_range.end,
             i,
             (void *)reg_range.start,
             (void *)reg_range.end);
      return false;
    }
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
      if (!(q == ihdr && p == ihdr->regions + ihdr->region_cap)) {
        other_reg_range = region_range(ihdr->regions[j]);
        reg_range = region_range(ihdr->regions[i]);
        printf("ERROR: regions have gap: #%d (%p,%p) and #%d (%p,%p)\n",
               j,
               (void *)other_reg_range.start,
               (void *)other_reg_range.end,
               i,
               (void *)reg_range.start,
               (void *)reg_range.end);
        return false;
      } else {
        // case: gap is the IntegHeader
        // continue;
      }
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

// -------------------------------------------------------------------------------------------------

[[gnu::used]]
const uint32_t fcstab_32[256] = {
  0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
  0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
  0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
  0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
  0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
  0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
  0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
  0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
  0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
  0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
  0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
  0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
  0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
  0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
  0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
  0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
  0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
  0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
  0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
  0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
  0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
  0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
  0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
  0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
  0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
  0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
  0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
  0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
  0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
  0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
  0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
  0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

void
integ_prep(IntegHeader *ihdr)
{
  size_t i, size;
  void *p;

  printf("entered integ_prep!\n");
  printf("ihdr:\n");
#define FIELD(name, fmt) printf("\t" #name "=" fmt "\n", ihdr->name)
  FIELD(self_hash, "%08x");
  FIELD(lr, "%p");
  FIELD(f, "%p");
  FIELD(f_arg, "%p");
  FIELD(seed, "%p");
  FIELD(flags, "%08x");
  FIELD(stack.sp, "%08x");
  FIELD(stack.stack_lo, "%08x");
  FIELD(region_count, "%d");
  FIELD(region_cap, "%d");

  for (i = 0; i < ihdr->region_count; i++) {
    p = ihdr->regions[i].p;
    size = ihdr->regions[i].size;

    // Double check that the region is not carrying corruption from a previous iteration.
    assert(!(ihdr->regions[i].flags & (IRF_Corrupted | IRF_CorruptedSomewhere)));

    ihdr->regions[i].cksum = 0;

    // If IRF_Mutable, we just ignore this region.
    if (ihdr->regions[i].flags & IRF_Mutable)
      continue;

    // If IRF_Purgable, we zero this region.
    if (ihdr->regions[i].flags & IRF_Purgable) {
      // TODO: randomize
      // TODO: faster memset
      memset(p, 0, size);
      continue;
    }

    // If not (IRF_Purgable or IRF_Mutable), then we hash this region.
    // However, the stack needs a bit of special care.

    if (ihdr->regions[i].flags & IRF_Stack) {
      range reg_range = region_range(ihdr->regions[i]);
      if (range_contains(reg_range, ihdr->stack.sp)) {
        ihdr->regions[i].cksum =
          hash_memory(ihdr->seed, (void *)ihdr->stack.sp, (uintptr_t)p + size - ihdr->stack.sp);
        if (ihdr->flags & IHF_ExpectStackCleanup)
          memset(p, 0, ihdr->stack.sp - (uintptr_t)p);
        continue;
      } else if (reg_range.end <= ihdr->stack.sp) {
        // XXX: wait, this should really be an error
        panic("ERROR: IRF_Stack region that ends below stack pointer\n");
        // either IRF_Mutable or IRF_Purgable, even if not marked so
        // if (ihdr->flags & IHF_ExpectStackCleanup)
        //   memset(p, 0, size);
        continue;
      }
    }

    ihdr->regions[i].cksum = hash_memory(ihdr->seed, ihdr->regions[i].p, ihdr->regions[i].size);
  }

  printf("exiting integ_prep!\n");
}

static const char *PRECHK_STRINGS[] = {
  "precheck OK",
  "precheck failed: pointer to IntegHeader corrupted",
  "precheck failed: IntegHeader corrupted",
  "precheck failed: microstack corrupted",
};

bool
check_purgable(void *p, size_t size)
{
  volatile uint8_t *q;
  q = p;
  while (size--) {
    if (*q) {
      printf("byte written at %p: 0x%hhx\n", q, *q);
      return false;
    }
    q++;
  }
  return true;
}

IntegResult
integ_chk(void *maybe_ihdr, uint32_t prechk)
{
  IntegHeader *ihdr;
  IntegResult ires;

  void *sp;
  __asm__ ("mov %0, sp\n" : "=r"(sp));
  printf("sp = %p\n", sp);

  if (prechk) {
    printf("integ_chk: %s (%d)\n", prechk <= 3 ? PRECHK_STRINGS[prechk] : "<unknown>", prechk);
    if (prechk == 3) {
      ihdr = maybe_ihdr;
      printf("microstack: found %08x at %p\n", (uint32_t)ihdr->f_arg, ihdr->f);
    } else if (prechk == 2) {
      ihdr = maybe_ihdr;
      printf("IntegHeader corrupted: expected checksum %08x, got %08x\n",
             ihdr->self_hash,
             (uint32_t)ihdr->f);
    }

    aux_uart_flush_tx_fifo();
    pwrman_reset();
  }

  printf("entered integ_chk!\n");

  ihdr = maybe_ihdr;

  ires.state = IS_Clean;

  // now the rest :)

  size_t i, size;
  uint8_t *p;
  uint32_t hash;

  for (i = 0; i < ihdr->region_count; i++) {
    p = ihdr->regions[i].p;
    size = ihdr->regions[i].size;

    if (ihdr->regions[i].flags & IRF_Mutable)
      continue;

    if (ihdr->regions[i].flags & IRF_Purgable) {
      if (!check_purgable(p, size)) {
        ihdr->regions[i].flags |= IRF_Corrupted;
        ires.state = ihdr->regions[i].flags & IRF_Critical ? IS_AbEnd : IS_AbCon;
      }
      continue;
    }

    if (ihdr->regions[i].flags & IRF_Stack) {
      range reg_range = region_range(ihdr->regions[i]);
      if (range_contains(reg_range, ihdr->stack.sp)) {
        hash =
          hash_memory(ihdr->seed, (void *)ihdr->stack.sp, (uintptr_t)p + size - ihdr->stack.sp);
        if (hash != ihdr->regions[i].cksum) {
          printf("hash mismatch on region #%d (%p,%p): expected %08x got %08x\n",
                 i,
                 p,
                 p + size,
                 ihdr->regions[i].cksum,
                 hash);
          ihdr->regions[i].flags |= IRF_CorruptedSomewhere;
          ires.state = ihdr->regions[i].flags & IRF_Critical ? IS_AbEnd : IS_AbCon;
        }
        if (ihdr->flags & IHF_ExpectStackCleanup) {
          if (!check_purgable(p, ihdr->stack.sp - (uintptr_t)p)) {
            ihdr->regions[i].flags |= IRF_Corrupted;
            ires.state = ihdr->regions[i].flags & IRF_Critical ? IS_AbEnd : IS_AbCon;
          }
        }
        continue;
      } else if (reg_range.end <= ihdr->stack.sp) {
        // XXX: wait, this should really be an error
        panic("ERROR: IRF_Stack region that ends below stack pointer\n");
        // if (ihdr->flags & IHF_ExpectStackCleanup) {
        //   if (!check_purgable(p, size)) {
        //     ihdr->regions[i].flags |= IRF_Corrupted;
        //     ires.state = ihdr->regions[i].flags & IRF_Critical ? IS_AbEnd : IS_AbCon;
        //   }
        // }
        continue;
      }
    }

    hash = hash_memory(ihdr->seed, p, size);
    if (hash != ihdr->regions[i].cksum) {
      printf("hash mismatch on region #%d (%p,%p): expected %08x got %08x\n",
             i,
             p,
             p + size,
             ihdr->regions[i].cksum,
             hash);
      ihdr->regions[i].flags |= IRF_CorruptedSomewhere;
      ires.state = ihdr->regions[i].flags & IRF_Critical ? IS_AbEnd : IS_AbCon;
    }
  }

  if(ires.state == IS_AbEnd)
    printf("\x1b[31m=== MEMORY CORRUPTED; CRITICAL CANNOT CONTINUE ===\x1b[0m\n");
  if(ires.state == IS_AbCon)
    printf("\x1b[33m=== MEMORY CORRUPTED; CONTINUING ===\x1b[0m\n");

  printf("exiting integ_chk!\n");

  return ires;
}

[[gnu::naked]]
IntegResult
integ_run(IntegHeader *ihdr, void (*f)(void *), void *arg)
{
  __asm__(
    // Write `f` and `arg` to `ihdr`
    "str r1, [r0, %[f]]\n"
    "str r2, [r0, %[f_arg]]\n"

    // Write lr to ihdr->lr
    "str lr, [r0, %[lr]]\n"

    // r0 is not caller-saved, and we don't have a persistent stack to push one of the callee-saved
    // registers to so we can put r0 into one of those. thus, we store directly in the '100' label
    // below.
    "adr r2, 100f\n"
    "str r0, [r2]\n"
    
    // "hash" r0 and put it in the 101 label up ahead
    // 9e 37 79 b1
    "mov r1, #(0x9e << 24)\n"
    "orr r1, r1, #(0x37 << 16)\n"
    "orr r1, r1, #(0x79 << 8)\n"
    "orr r1, r1, #(0xb1 << 0)\n"
    "mul r2, r0, r1\n"
    "adr r3, 101f\n"
    "str r2, [r3]\n"

    // capture the stack pointer and then repoint the stack to the special integrity checker stack
    "str sp, [r0, %[stack_sp]]\n"
    // "add sp, r0, %[microstack]\n"
    "mov sp, r0\n"

    // prep the integrity checker state
    // we push r1 to ensure stack alignment
    "push {r0, r1}\n"
    "bl integ_prep\n"
    "pop {r0, r1}\n"

    // zero out the microstack
    "add r2, r0, %[microstack]\n"
    "mov r3, #0\n"
    "mov r1, %[microstack_size]\n"
    "cmp r1, #0\n"
    "0:\n"
    "beq 1f\n"
    "str r3, [r2], #4\n"
    "subs r1, r1, #4\n"
    "b 0b\n"
    "1:\n"

    // restore the stack pointer
    // "mov sp, r1\n"
    "ldr sp, [r0, %[stack_sp]]\n"

    // hash the IntegHeader
    "push {r4-r6}\n"
    "mov r1, #0xffffffff\n"
    "add r2, r0, %[ihdr_hash_start]\n"
    "ldr r3, =fcstab_32\n"
    "add r4, r0, %[regions]\n"
    "ldr r5, [r0, %[region_cap]]\n"
    "add r4, r4, r5\n"
    "2:\n"
    "cmp r2, r4\n"
    "beq 3f\n"
    "ldrb r5, [r2], #1\n"
    // fcs = (fcs >> 8) ^
    // fcstab_32[
    //   (fcs ^ (*cp++)) & 0xff
    // ];
    "eor r5, r5, r1\n"
    "and r5, r5, #0xff\n"
    "ldr r6, [r3, r5, lsl #2]\n"
    "eor r1, r6, r1, lsr #8\n"
    "b 2b\n"
    "3:\n"
    "str r1, [r0, %[self_hash]]\n"
    "pop {r4-r6}\n"

    // zero out the low part of the real stack
    "ldr r1, [r0, %[stack_lo]]\n"
    "mov r2, #0\n"
    "4:\n"
    "cmp r1, sp\n"
    "beq 5f\n"
    "str r2, [r1], #4\n"
    "b 4b\n"
    "5:\n"

    // ---------------------------------------------------------------------------------------------
    // ABOUT TO CALL PAYLOAD

    // call f(f_arg)
    "ldr r1, [r0, %[f]]\n"
    "ldr r2, [r0, %[f_arg]]\n"
    "mov r0, r2\n"
    "blx r1\n"

    // ---------------------------------------------------------------------------------------------
    // RETURNED FROM PAYLOAD

    // now we get to check everything <-<

    // first, IntegHeader pointer
    "ldr r0, 100f\n"
    "ldr r1, 101f\n"
    "mov r3, #(0x9e << 24)\n"
    "orr r3, r3, #(0x37 << 16)\n"
    "orr r3, r3, #(0x79 << 8)\n"
    "orr r3, r3, #(0xb1 << 0)\n"
    "mul r2, r0, r3\n"
    "cmp r1, r2\n"
    "beq 6f\n"
    "mov r1, #1\n"
    "b 20f\n"

    // second, microstack
    "6:\n"
    "add r1, r0, %[microstack]\n"
    "mov r2, %[microstack_size]\n"
    "7:\n"
    "ldr r3, [r1], #4\n"
    "tst r3, r3\n"
    "beq 8f\n"
    "str r1, [r0, %[f]]\n"
    "sub r3, r3, #4\n"
    "str r3, [r0, %[f_arg]]\n"
    "mov r1, #3\n"
    "b 20f\n"
    "8:\n"
    "subs r2, r2, #4\n"
    "bne 7b\n"

    // third, self_hash
    "push {r4-r6}\n"
    "mov r1, #0xffffffff\n"
    "add r2, r0, %[ihdr_hash_start]\n"
    "ldr r3, =fcstab_32\n"
    "add r4, r0, %[regions]\n"
    "ldr r5, [r0, %[region_cap]]\n"
    "add r4, r4, r5\n"
    "9:\n"
    "cmp r2, r4\n"
    "beq 10f\n"
    "ldrb r5, [r2], #1\n"
    // fcs = (fcs >> 8) ^
    // fcstab_32[
    //   (fcs ^ (*cp++)) & 0xff
    // ];
    "eor r5, r5, r1\n"
    "and r5, r5, #0xff\n"
    "ldr r6, [r3, r5, lsl #2]\n"
    "eor r1, r6, r1, lsr #8\n"
    "b 9b\n"
    "ldr r2, [r0, %[self_hash]]\n"
    "cmp r1, r2\n"
    "beq 10f\n"
    "str r1, [r0, %[f]]\n"
    "mov r1, #2\n"
    "10:\n"
    "pop {r4-r6}\n"

    "mov r1, #0\n"
    // tail call to integ_chk
    "20:\n"
    // "add sp, r0, %[microstack]\n"
    "mov sp, r0\n"
    "push {r0-r1}\n"
    "bl integ_chk\n"
    "pop {r0-r1}\n"
    "ldr r0, 100f\n"
    "ldr lr, [r0, %[lr]]\n"
    "ldr sp, [r0, %[stack_sp]]\n"
    "bx lr\n"

    "100: .word 0\n" // ihdr*
    "101: .word 0\n" // ihdr* hash
    :
    : [microstack] "i"(-MICROSTACK_SIZE),
      [microstack_size] "i"(MICROSTACK_SIZE),
      [f] "i"(offsetof(IntegHeader, f)),
      [f_arg] "i"(offsetof(IntegHeader, f_arg)),
      [ihdr_hash_start] "i"(offsetof(IntegHeader, f)),
      [regions] "i"(offsetof(IntegHeader, regions)),
      [region_cap] "i"(offsetof(IntegHeader, region_cap)),
      [self_hash] "i"(offsetof(IntegHeader, self_hash)),
      [stack_sp] "i"(offsetof(IntegHeader, stack) + offsetof(StackInteg, sp)),
      [stack_lo] "i"(offsetof(IntegHeader, stack) + offsetof(StackInteg, stack_lo)),
      [lr] "i"(offsetof(IntegHeader, lr)));
}
