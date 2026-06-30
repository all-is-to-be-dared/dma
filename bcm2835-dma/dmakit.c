#include <string.h>

#include <bcm2835-dma/private.h>
#include <bcm2835/platform.h>
#include <bcm2835/extra.h>
#include <bcm2835/ptags.h>
#include <bcm2835/arch.h>
#include <generic/bits.h>
#include <printf/printf.h>




const char* channel_reservations[16] = {};
static bool did_init_reservations = false;

void
dmakit_init_channels(void)
{
  uint32_t chan_mask, i;

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

enum { PROBE_MAX = 32 };
struct probe probes[32] = {};
size_t probecnt = 0;

void
_Probe(const char *func, const char *name, volatile void *data, size_t width)
{
  probecnt = 0;
  printf("_Probe: %lu\n", probecnt);
  printf("_Probe: %lx\n", probecnt);
  printf("_Probe: %lld\n", (uint64_t)probecnt);
  printf("_Probe: %llx\n", (uint64_t)probecnt);

  if(probecnt >= PROBE_MAX)
    panic("no more probe slots to allocate (%zu>%d)\n", probecnt, PROBE_MAX);

  // printf("Attaching probe #%ld: %s:%s to %p:+%zu\n", probecnt, func, name, data, width);

  panic("boom\n");

  probes[probecnt++] = (struct probe){func, name, data, width};
}

void
ReportProbeInfo(void)
{
  int i,j;
  struct probe *p;

  printf("Probe count: %d\n", probecnt);
  for(i=0;i<probecnt;i++) {
    p = probes + i;
    printf("%s:%s (%p:+%zu): ", p->func, p->name, p->data, p->size);
    for(j=0;j<p->size;j++) {
      printf("%02hhx", p->data + p->size - 1 - j);
    }
    printf("\n");
  }
}
