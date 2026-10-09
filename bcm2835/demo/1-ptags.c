#include <bcm2835/arch.h>
#include <bcm2835/mmu.h>
#include <bcm2835/platform.h>
#include <bcm2835/ptags.h>

#include <generic/assert.h>
#include <generic/backtrace.h>
#include <generic/macros.h>
#include <generic/math.h>
#include <generic/printf.h>


void
main(struct elf_boot_args *boot_args) {
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

  // -----------------------------------------------------------------------------------------------

  int i;

  printf("    \x1b[4m\x1b[2m       Clock     Nominal    Measured\n\x1b[0m");
  for(i = 0;i < PCID_CLOCK_COUNT;i++) {
    printf(
      "    %12s% 12d% 12d\n", PTAG_CLOCK_NAMES[i], ptag_get_nominal_clock_rate(i), ptag_get_measured_clock_rate(i));
  }
  
  // -----------------------------------------------------------------------------------------------

  struct ptag_mem_region arm_mem, vc_mem;
  
  arm_mem = ptag_get_arm_memory();
  vc_mem = ptag_get_vc_memory();

  printf("Memory regions:\n");
  printf("\t\x1b[3mName\tBase    \tSize    \x1b[0m\n");
  printf("\tARM\t%08x\t%08x\n", arm_mem.base, arm_mem.size);
  printf("\tVC\t%08x\t%08x\n", vc_mem.base, vc_mem.size);
  printf("\n");

  // -----------------------------------------------------------------------------------------------
  
  uint16_t chanmask;

  chanmask = ptag_get_dma_channel_mask();
  printf("Channel mask: %08x\n", chanmask);
  printf("Available DMA channels:");
  for (i = 0; i < 16; i++)
    if (chanmask & (1 << i))
      printf(" \x1b[1m\x1b[32m[%d]\x1b[0m", i);
    else
      printf(" %d", i);
  printf("\n\n");


  // -----------------------------------------------------------------------------------------------

  printf("\nDONE.\n");
  aux_uart_flush_tx_fifo();
}
