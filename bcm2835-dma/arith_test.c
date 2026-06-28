#include <bcm2835-dma/private.h>
#include <bcm2835/platform.h>
#include <bcm2835/extra.h>
#include <bcm2835/ptags.h>
#include <printf/printf.h>

void
main()
{
  struct ptag_mem_region arm_mem, vc_mem;
  uint32_t chanmask, i;

  gpio_pin_set_function(14, FSEL_ALT5);
  gpio_pin_set_function(15, FSEL_ALT5);

  aux_uart_init(1152000, 250'000'000);

  systmr_delay_ms(1000);
  printf(__FILE__ ": starting\n");

  arm_mem = ptag_get_arm_memory();
  vc_mem = ptag_get_vc_memory();

  printf("Memory regions:\n");
  printf("\t\x1b[3mName\tBase    \tSize    \x1b[0m\n");
  printf("\tARM\t%08x\t%08x\n", arm_mem.base, arm_mem.size);
  printf("\tVC\t%08x\t%08x\n", vc_mem.base, vc_mem.size);
  printf("\n");

  chanmask = ptag_get_dma_channel_mask();
  printf("Channel mask: %08x\n", chanmask);
  printf("Available DMA channels:");
  for (i = 0; i < 16; i++)
    if (chanmask & (1 << i))
      printf(" \x1b[1m\x1b[32m[%d]\x1b[0m", i);
    else
      printf(" %d", i);
  printf("\n\n");

  dmakit_init_channels();
  struct dmakit_reservation rsv;
  assert(dmakit_channel_acquire(&rsv, false, "ArithTest"));
  dmakit_dump_channel_reservations();

  aux_uart_flush_tx_fifo();
}
