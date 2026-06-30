#include <string.h>
#include <inttypes.h>
#include <bcm2835/arch.h>
#include <bcm2835-dma/private.h>
#include <bcm2835/platform.h>
#include <bcm2835/extra.h>
#include <bcm2835/ptags.h>
#include <bcm2835-dma/emit.h>
#include <bcm2835-dma/arith.h>
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
  struct dma_run_info runinfo;

  pmu_enable(); // enable cycle_count_read/write

  assert(dmakit_channel_acquire(&rsv, false, "ArithTest")); // Reserve a channel and dump RSVs
  dmakit_dump_channel_reservations();
  printf("\n");
  
  run_initializers(); // run all the magic INITIALIZERs

  cblk blocks[1024], *start;
  emit_ctx ctx = {
    .base = blocks,
    .p = blocks,
    .cap = sizeof blocks / sizeof *blocks
  };
  // static uint8_t alignas(0x4000) ARENA[0x4000];
  // arena arena = {
  //   .start = ARENA,
  //   .p = ARENA,
  //   .end = ARENA + sizeof ARENA,
  //   .util = 0,
  // };
  
  {
    volatile uint32_t a, b;
    a = 0x12345678;
    b = 0;

    start = Here(&ctx);
    Not(&ctx, 4, bus(&b), bus(&a));
    End(&ctx);

    for(int i = 0;i < 4;i++) {
      b = 0;
      printf("Timing 4-wide Not... ");
      assert(b == 0);
      runinfo = dmakit_timed_run(rsv, start);
      assert(b == ~0x12345678, "post: b = %08"PRIx32" (expected %08"PRIx32")\n", b, ~a);
      printf("%dcy\n", runinfo.cycle_end);
    }
  }

  {
    volatile uint32_t a, b, c;
    a = 0x12345678;
    b = 7;

    start = Here(&ctx);
    Sll8(&ctx, 4, bus(&c), bus(&a), bus(&b));
    End(&ctx);

    for(int i = 0;i < 4;i++) {
      c = 0;
      printf("Timing 4-wide Sll8... ");
      assert(c == 0);
      runinfo = dmakit_timed_run(rsv, start);
      ReportProbeInfo();
      assert(a == 0x12345678);
      assert(b == 7);
      assert(c == (a << b), "post: c = %08"PRIx32" (expected %08"PRIx32")\n", c, a<<b);
      printf("%dcy\n", runinfo.cycle_end);
    }
  }

  printf("Finished.\n");
  
  aux_uart_flush_tx_fifo();
}
