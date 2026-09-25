#include <bcm2835-dma/private.h>
#include <bcm2835/platform.h>
#include <bcm2835/extra.h>
#include <bcm2835/arch.h>
#include <generic/bits.h>
#include <generic/printf.h>

struct dma_run_info
__dma_timed_run(hw_dmachan_t* chan, cblk* init_blk)
{
  uint32_t cycle_end, status;
  uint64_t us_start, us_end;
  busad init_blk_bus;


  assert(init_blk);
  init_blk_bus = arm_to_bus((uintptr_t)init_blk);

  dsb();

  // clear the error bits
  chan->debug &= ~(DMA_DEBUG_ERROR_READ | DMA_DEBUG_ERROR_READ | DMA_DEBUG_ERROR_AXI_READ_LAST);

  // Sanity check
  assert(!__dma_is_error(chan));
  assert(!__dma_is_active(chan));

  // SAFETY: In order for the DMA to work correctly, it's required that
  //           1. the compiler doesn't hoist the DMA MMIO
  //           2. all writes in the pipeline are flushed
  //           3. L1 is flushed to the VC's L2
  COMPILER_BARRIER;
  dsb();
  flush_dcache();

  us_start = systmr_read_raw();

  dsb();
  
  cycle_count_write(0);
  chan->conblk_ad = init_blk_bus;
  chan->cs = DMA_CS_START;

  while (__dma_is_active(chan))
    ;

  cycle_end = cycle_count_read();
  us_end = systmr_read_raw();

  dsb();

  assert(!chan->conblk_ad._0);

  status = 0;

  if (chan->cs & DMA_CS_ERROR)
    status |= DMA_ERROR_ANY;
  if (chan->debug & DMA_DEBUG_ERROR_READ)
    status |= DMA_ERROR_READ;
  if (chan->debug & DMA_DEBUG_ERROR_FIFO)
    status |= DMA_ERROR_FIFO;
  if (chan->debug & DMA_DEBUG_ERROR_AXI_READ_LAST)
    status |= DMA_ERROR_AXI_READ_LAST;

  if (!(status & DMA_ERROR_ANY) &&
      (status & (DMA_ERROR_READ | DMA_ERROR_FIFO | DMA_ERROR_AXI_READ_LAST))) {
    panic("dma_timed_run: CS has ERROR bit set, but DEBUG has no error bits set\n");
  } else if ((status & DMA_ERROR_ANY) &&
             !(status & (DMA_ERROR_READ | DMA_ERROR_FIFO | DMA_ERROR_AXI_READ_LAST))) {
    panic("dma_timed_run: DEBUG has error bits set, but CS has ERROR bit clear\n");
  }

  if (chan->cs & DMA_CS_OUTSTANDING_WRITES)
    status |= DMA_OUTSTANDING_WRITES;
  if (chan->cs & DMA_CS_PAUSED_BY_DREQ)
    status |= DMA_PAUSED_FOR_DREQ;

  dsb();

  // SAFETY: We also might want to interact with the values of the things the DMA wrote to, in
  //         which case we absolutely cannot allow those to be lifted above the DMA MMIO.
  COMPILER_BARRIER;

  return (struct dma_run_info){
    .cycle_start = 0,
    .cycle_end = cycle_end,
    .us_start = us_start,
    .us_end = us_end,
    .status = status,
  };
}

hw_dmachan_t*
__dma_channel(enum dma_channel_no no)
{
  static hw_dmachan_t *dma0_14 = (hw_dmachan_t*)DMA0_BASE, *dma15 = (hw_dmachan_t*)DMA15_BASE;
  if (no < DMA_CHAN15)
    return dma0_14 + no;
  else if (no == DMA_CHAN15)
    return dma15;
  else
    panic("Invalid DMA channel: %d\n", no);
}
