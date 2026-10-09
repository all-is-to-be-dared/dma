#include <generic/printf.h>
#include <generic/assert.h>
#include <generic/backtrace.h>
#include <generic/macros.h>

#include <bcm2835/arch.h>
#include <bcm2835/mmu.h>
#include <bcm2835/platform.h>

#include <pup/protocol.h>

#include "sopter.h"

void main(struct elf_boot_args *boot_args)
{
  gpio_pin_set_function(14, FSEL_ALT5);
  gpio_pin_set_function(15, FSEL_ALT5);
  aux_uart_init(1152000);
  systmr_delay_ms(1000);
  printf(__FILE__ ": starting\n");

  if(!backtrace_enable(boot_args->elf))
    printf("\n[ \x1b[33mWARNING\x1b[0m: Backtrace functionality not enabled! ]\n\n");

  static struct mmu_translation_table ttb = {};
  mmu_init(&ttb);
  icache_set_enabled(true);
  brpdx_set_enabled(true);
  dcache_set_enabled(true);

  PhysicalPageAllocator ppa;

  uintptr_t preclaims[][2] = {
    
  };

  ppa_init(&ppa, sizeof preclaims / sizeof preclaims[0], preclaims);

  printf("ssmask: %08x\n", ppa.supersection_mask);

  for(int i = 0;i<32;i++)
    printf("al.ss %d: %p\n", i, (void*)ppa_alloc(&ppa, RK_Supersection));

  assert(ppa_alloc(&ppa, RK_SmallPage) == UINTPTR_MAX);
  assert(ppa_alloc(&ppa, RK_LargePage) == UINTPTR_MAX);
  assert(ppa_alloc(&ppa, RK_Section) == UINTPTR_MAX);
  assert(ppa_alloc(&ppa, RK_Supersection) == UINTPTR_MAX);

  ppa_free(&ppa, 0x1000000, RK_Supersection);

  for(int i = 0;i < 16;i++)
    printf("al.s %d: %p\n", i, (void*)ppa_alloc(&ppa, RK_Section));

  assert(ppa_alloc(&ppa, RK_SmallPage) == UINTPTR_MAX);
  assert(ppa_alloc(&ppa, RK_LargePage) == UINTPTR_MAX);
  assert(ppa_alloc(&ppa, RK_Section) == UINTPTR_MAX);
  assert(ppa_alloc(&ppa, RK_Supersection) == UINTPTR_MAX);

  ppa_free(&ppa, 0x1000000, RK_Section);

  for(int i = 0;i < 16;i++)
    printf("al.lp %d: %p\n", i, (void*)ppa_alloc(&ppa, RK_LargePage));

  assert(ppa_alloc(&ppa, RK_SmallPage) == UINTPTR_MAX);
  assert(ppa_alloc(&ppa, RK_LargePage) == UINTPTR_MAX);
  assert(ppa_alloc(&ppa, RK_Section) == UINTPTR_MAX);
  assert(ppa_alloc(&ppa, RK_Supersection) == UINTPTR_MAX);

  printf("\nDONE.\n");
  aux_uart_flush_tx_fifo();
}
