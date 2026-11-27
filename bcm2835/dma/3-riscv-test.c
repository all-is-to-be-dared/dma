#include <inttypes.h>

#include <bcm2835/arch.h>
#include <bcm2835/platform.h>
#include <generic/assert.h>
#include <bcm2835/ptags.h>

#include <bcm2835/dma/private.h>
#include <bcm2835/dma/emit.h>
#include <bcm2835/dma/arith.h>
#include <bcm2835/dma/rv_emu.h>

#include <generic/math.h>
#include <generic/printf.h>

double
meanlf(const uint64_t *d, size_t n)
{
  uint64_t tot;
  size_t i;

  if (!n)
    return 0.;

  tot = 0;
  for (i = 0; i < n; i++)
    tot += d[i];
  return (double)tot / (double)n;
}
double
stdevlf(const uint64_t *d, size_t n)
{
  double mean;
  uint64_t acc;
  size_t i;

  if (!n)
    return 0;

  mean = meanlf(d, n);

  acc = 0;
  for (i = 0; i < n; i++)
    acc += d[i] * d[i];

  return __builtin_sqrt((double)acc / (double)n - mean * mean);
}

arena ARENA;

void
main(void)
{
  struct ptag_mem_region arm_mem, vc_mem;
  uint32_t i;
  uint16_t chanmask;

  gpio_pin_set_function(14, FSEL_ALT5);
  gpio_pin_set_function(15, FSEL_ALT5);

  aux_uart_init(1152000);

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

  HYPER = (void *)0x10000000;
  QHAT = HYPER + (1 << 24);
  RHAT = QHAT + (1 << 24);
  volatile uint8_t *ARENA_BACKING = RHAT + (1 << 24);
  enum {
    ARENA_SIZE = 1 << 24,
  };
  ARENA = (arena){
    .start = (void*)ARENA_BACKING,
    .p = (void*)ARENA_BACKING,
    .end = (void*)ARENA_BACKING + (ARENA_SIZE),
    .util = 0,
  };

  dmakit_init_channels();
  struct dmakit_reservation rsv;
  struct dma_run_info runinfo;

  pmu_enable(); // enable cycle_count_read/write

  assert(dmakit_channel_acquire(&rsv, false, "ArithTest")); // Reserve a channel and dump RSVs
  dmakit_dump_channel_reservations();
  printf("\n");

  uint32_t init_start = cycle_count_read(), init_end;
  run_initializers(); // run all the magic INITIALIZERs
  init_end = cycle_count_read();
  printf("initializers ran in %dcy\n", init_end - init_start);

  // --- THING ---

  enum {
    BLOCK_CNT = 32768,
  };
  static cblk blocks[BLOCK_CNT];
  emit_ctx ctx = { .base = blocks, .p=blocks, .cap=BLOCK_CNT };

  uint64_t overhead = dma_get_launch_overhead(&ctx, rsv);

  printf("Basal overhead (1xDST_IGN): %lldns\n", overhead);

  // RISC-V emulation tests go here
  
  printf("Finished.\n");

  aux_uart_flush_tx_fifo();
}
