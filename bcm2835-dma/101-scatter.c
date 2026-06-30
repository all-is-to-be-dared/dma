#include <string.h>
#include <bcm2835/arch.h>
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
  struct dma_run_info runinfo;

  pmu_enable();

  assert(dmakit_channel_acquire(&rsv, false, "ArithTest"));
  dmakit_dump_channel_reservations();
  printf("\n");

  enum { SIZE = 1024 };
  volatile uint32_t a0[SIZE], a1[SIZE];

  for(int i = 0;i<4;i++){
    memset((void*)a0, 0, sizeof a0);
    memset((void*)a1, 0, sizeof a1);
    a0[0] = 0x12345678;
    cblk cb = { .ti = TI_DST_INC | TI_SRC_INC,
                .src_addr = arm_to_bus((uintptr_t)a0),
                .dst_addr = arm_to_bus((uintptr_t)a1),
                .txfr_len = 4,
                .stride = 0,
                .next_cb = {0},
              };
    printf("Timing 4 byte copy... ");
    assert(a1[0] == 0);
    runinfo = dmakit_timed_run(rsv, &cb);
    assert(a1[0] == 0x12345678, "post: a1[0] = %08lx\n", a1[0]);
    printf("%dcy\n", runinfo.cycle_end);
  }

  cblk cb[16];

  for(int n = 1; n < 16; n += 1){
    printf("N = %d\n", n);
    memset((void*)a0, 0, sizeof a0);
    memset((void*)a1, 0, sizeof a1);
    a0[0] = 0x12345678;

    memset(cb, 0, sizeof cb);
    for(int i = 0;i<n;i++) {
      cb[i].ti = TI_DST_INC | TI_SRC_INC;
      cb[i].src_addr = arm_to_bus((uintptr_t)a0);
      cb[i].dst_addr = arm_to_bus((uintptr_t)a1 + i * 16 * 4);
      cb[i].txfr_len = 4;
      cb[i].next_cb = arm_to_bus((uintptr_t)(cb + i + 1));
    }
    cb[n-1].next_cb = (busad){0};

    printf("Timing %dx4 byte linear scatter... ", n);
    runinfo = dmakit_timed_run(rsv, cb);
    for(int i = 0;i<SIZE;i++)
      assert(a1[i] == (i % 16 || (i/16) >= n ? 0: 0x12345678));
    printf("%dcy\n", runinfo.cycle_end);

    memset((void*)a0, 0, sizeof a0);
    memset((void*)a1, 0, sizeof a1);
    a0[0] = 0x12345678;
    a0[1] = 0x12345678;
    a0[2] = 0x12345678;
    a0[3] = 0x12345678;

    memset(cb, 0, sizeof cb);
    cb[0].ti = TI_DST_INC | TI_2DMODE;
    cb[0].src_addr = arm_to_bus((uintptr_t)a0);
    cb[0].dst_addr = arm_to_bus((uintptr_t)a1);
    cb[0].txfr_len = length(n - 1, 4);
    cb[0].stride = stride(60, 0);
    cb[0].next_cb = (busad){0};
    printf("Timing %dx4 byte 2D scatter... ", n);
    runinfo = dmakit_timed_run(rsv, cb);
    for(int i = 0;i<SIZE;i++) {
      uint32_t exp = (i % 16 || (i/16) >= n ? 0: 0x12345678);
      if(a1[i] != exp)
        printf("a1[%d] = %08lx expected %08lx", i, a1[i], exp);
    }
    printf("%dcy\n", runinfo.cycle_end);
  }

  printf("Finished.\n");
  
  aux_uart_flush_tx_fifo();
}
