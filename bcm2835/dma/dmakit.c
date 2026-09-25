#include <string.h>

#include <bcm2835-dma/private.h>
#include <bcm2835/platform.h>
#include <bcm2835/extra.h>
#include <bcm2835/ptags.h>
#include <bcm2835/arch.h>
#include <generic/bits.h>
#include <generic/printf.h>




const char* channel_reservations[DMA_CHAN_COUNT] = {};
static bool did_init_reservations = false;

void
dmakit_init_channels(void)
{
  int i;
  uint16_t chan_mask;

  assert(!did_init_reservations);
  did_init_reservations = true;

  chan_mask = ptag_get_dma_channel_mask();

  for (i = 0; i < DMA_CHAN_COUNT; ++i) {
    channel_reservations[i] = chan_mask & (1 << i) ? NULL : "VideoCore";
  }
}

bool
dmakit_channel_acquire(struct dmakit_reservation* rsv, bool allow_lite, const char* who)
{
  int i;

  for (i = 0; i < 16; ++i) {
    if (!channel_reservations[i]) {
      if (!allow_lite && dmakit_channel_is_lite(i))
        continue;

      channel_reservations[i] = who;
      rsv->chan = i;
      return true;
    }
  }

  return false;
}

void
dmakit_channel_release(struct dmakit_reservation rsv, const char* who)
{
  assert(!strcmp(who, channel_reservations[rsv.chan]));
  assert(!dmakit_channel_is_active(rsv.chan));

  channel_reservations[rsv.chan] = NULL;
}

void
dmakit_dump_channel_reservations(void)
{
  int i;

  printf("Channel reservations:\n");
  for (i = DMA_CHAN0; i < DMA_CHAN_COUNT; ++i) {
    printf("\t%2d - %s\n", i, channel_reservations[i]);
  }
}




bool
dmakit_channel_is_lite(enum dma_channel_no chan_no)
{
  return chan_no >= DMA_CHAN7 && chan_no < DMA_CHAN15;
}

bool
dmakit_channel_is_active(enum dma_channel_no chan_no)
{
  hw_dmachan_t* chan;
  bool r;

  chan = __dma_channel(chan_no);
  assert(chan);

  dsb();
  r = __dma_is_active(chan);
  dsb();

  return r;
}




struct dma_run_info
dmakit_timed_run(struct dmakit_reservation rsv, cblk* init_blk)
{
  hw_dmachan_t* chan;
  struct dma_run_info runinfo;

  chan = __dma_channel(rsv.chan);
  assert(chan);

  runinfo = __dma_timed_run(chan, init_blk);

  return runinfo;
}




