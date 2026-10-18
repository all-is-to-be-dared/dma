#include "bcm2835/arch.h"
#include <bcm2835/platform.h>
#include <bcm2835/ptags.h>
#include <bcm2835/mmu.h>

#include <generic/assert.h>
#include <generic/math.h>
#include <generic/printf.h>

void pcm_init(void)
{
  gpio_pin_set_function(18, FSEL_ALT0);
  gpio_pin_set_function(19, FSEL_ALT0);
  gpio_pin_set_function(21, FSEL_ALT0);
}

static
void
run(void)
{
  pcm_init();
}

// -------------------------------------------------------------------------------------------------

void
main(struct elf_boot_args *bootargs)
{
  gpio_pin_set_function(14, FSEL_ALT5);
  gpio_pin_set_function(15, FSEL_ALT5);

  aux_uart_init(1152000);
  systmr_delay_ms(500);
  printf(__FILE__ ": starting\n");
  static alignas(0x4000) struct mmu_translation_table ttb = {};
  mmu_init(&ttb);
  icache_set_enabled(true);
  brpdx_set_enabled(true);
  dcache_set_enabled(true);
  pmu_enable();

  // -----------------------------------------------------------------------------------------------
  
  run();

  // -----------------------------------------------------------------------------------------------

  printf("\nDONE\n");
  aux_uart_flush_tx_fifo();
}
