#include <stdint.h>
#include <string.h>
#include <inttypes.h>

#include <bcm2835/platform.h>
#include <bcm2835/extra.h>
#include <bcm2835/ptags.h>
#include <bcm2835/arch.h>

#include <generic/printf.h>

enum mbox_buf_code
{
  PROCESS_REQUEST = 0,
  REQUEST_OK = (1UL << 31),
  REQUEST_ERR = (1UL << 31) | 1,
};
struct mbox_buf_hdr
{
  uint32_t size;
  enum mbox_buf_code code;
};

struct mbox_tag_hdr
{
  uint32_t ident;
  uint32_t vbuf_size;
  struct
  {
    uint32_t resp_size : 31;
    uint32_t is_response : 1;
  } code;
};
static inline struct mbox_tag_hdr
mk_tag_hdr(uint16_t ident0, uint16_t ident1, uint32_t vbuf_size)
{
  return (struct mbox_tag_hdr){ .ident = (ident0 << 16) | ident1,
                                .vbuf_size = vbuf_size,
                                .code = { .is_response = 0, .resp_size = 0 } };
}

enum mbox_chan
{
  ARM_TO_VC = 8,
};


static inline bool
mail(enum mbox_chan channel, void* tag, size_t tagsz)
{
  // SAFETY: This is probably a whole truckload of undefined behavior.
  // TODO: This should probably do some cache flushing.

  uintptr_t bufaddr;
  uint32_t writev, readv;
  volatile struct mbox_buf_hdr* buf_hdr;
  volatile uint8_t alignas(16) buf[sizeof *buf_hdr + tagsz + 4];

  // volatile struct mbox_tag_hdr* tag_hdr;
  // volatile uint8_t* tag_vbuf;
  // size_t i;

  assert(sizeof buf == sizeof *buf_hdr + tagsz + 4);
  memset((void*)buf, 0, sizeof buf);
  buf_hdr = (struct mbox_buf_hdr*)buf;
  buf_hdr->size = sizeof buf;
  buf_hdr->code = PROCESS_REQUEST;
  memcpy((void*)(buf_hdr + 1), tag, tagsz);

  bufaddr = (uintptr_t)buf;
  assert(!(bufaddr & 0xf));
  assert(bufaddr < 0x2000'0000);
  writev = (0x4000'0000UL + bufaddr) | channel;

  // tag_hdr = (struct mbox_tag_hdr*)(buf_hdr + 1);
  // tag_vbuf = (volatile uint8_t*)(tag_hdr + 1);

  // printf("mail: INITIAL STATE ------------\n");
  // printf("mail: Buffer:\n");
  // printf("mail:   size = %d\n", buf_hdr->size);
  // printf("mail:   code = %08x\n", buf_hdr->code);
  // printf("mail: Tag:\n");
  // printf("mail:   iden = %08x\n", tag_hdr->ident);
  // printf("mail:   vbsz = %d\n", tag_hdr->vbuf_size);
  // printf("mail:   code = %08x\n", tag_hdr->code);
  // printf("mail:   vbuf =\x1b[1m");
  // for (i = 0; i < tagsz - 12; ++i) {
  //   if (i == tag_hdr->vbuf_size)
  //     printf("\x1b[0m");
  //   printf(" %02hhx", tag_vbuf[i]);
  // }
  // printf("\n");
  // printf("mail: Tail:\n");
  // printf("mail:   bytes =");
  // for (i = sizeof *buf_hdr + tagsz; i < sizeof buf; ++i) {
  //   printf(" %02hhx", buf[i]);
  // }
  // printf("\n");

  __asm__ volatile("" ::: "memory");

  // clean and invalidate dcache so that buffer is written out to L2
  flush_entire_dcache();
  // dsb to sync on the flush
  dsb();
  while (mbox->status1 & MBOX_FULL)
    ;
  mbox->write1 = writev;
  while (mbox->status0 & MBOX_EMPTY)
    ;
  readv = mbox->read0;
  dsb();

  __asm__ volatile("" ::: "memory");

  assert(writev == readv);

  // printf("mail: FINAL STATE ------------\n");
  // printf("mail: Buffer:\n");
  // printf("mail:   size = %d\n", buf_hdr->size);
  // printf("mail:   code = %08x\n", buf_hdr->code);
  // printf("mail: Tag:\n");
  // printf("mail:   iden = %08x\n", tag_hdr->ident);
  // printf("mail:   vbsz = %d\n", tag_hdr->vbuf_size);
  // printf("mail:   code = %08x\n", tag_hdr->code);
  // printf("mail:   vbuf =\x1b[1m");
  // for (i = 0; i < tagsz - 12; ++i) {
  //   if (i == tag_hdr->vbuf_size)
  //     printf("\x1b[0m");
  //   printf(" %02hhx", tag_vbuf[i]);
  // }
  // printf("\n");
  // printf("mail: Tail:\n");
  // printf("mail:   bytes =");
  // for (i = sizeof *buf_hdr + tagsz; i < sizeof buf; ++i) {
  //   printf(" %02hhx", buf[i]);
  // }
  // printf("\n");

  switch (buf_hdr->code) {
    case REQUEST_OK:
      memcpy(tag, (void*)(buf_hdr + 1), tagsz);
      return true;
    case REQUEST_ERR:
      return false;
    default:
      panic("mailbox got unexpected response code: %08x\n", buf_hdr->code);
  }
}

struct ptag_mem_region
ptag_get_arm_memory(void)
{
  struct
  {
    struct mbox_tag_hdr hdr;
    struct ptag_mem_region payload;
  } tag = { .hdr = mk_tag_hdr(1, 5, 8), .payload = {} };

  if (!mail(ARM_TO_VC, &tag, sizeof tag))
    panic("ptag_get_arm_memory: failed to query ARM memory size\n");

  assert(tag.hdr.code.is_response && tag.hdr.code.resp_size == 8);

  return tag.payload;
}

struct ptag_mem_region
ptag_get_vc_memory(void)
{
  struct
  {
    struct mbox_tag_hdr hdr;
    struct ptag_mem_region payload;
  } tag = { .hdr = mk_tag_hdr(1, 6, 8), .payload = {} };

  if (!mail(ARM_TO_VC, &tag, sizeof tag))
    panic("ptag_get_vc_memory: failed to query VC memory size\n");

  assert(tag.hdr.code.is_response && tag.hdr.code.resp_size == 8);

  return tag.payload;
}

uint16_t
ptag_get_dma_channel_mask(void)
{
  struct
  {
    struct mbox_tag_hdr hdr;
    uint32_t payload;
  } tag = { .hdr = mk_tag_hdr(6, 1, 4), .payload = 0 };

  if (!mail(ARM_TO_VC, &tag, sizeof tag))
    panic("ptag_get_dma_channels: failed to query VC for usable DMA channels\n");

  assert(tag.hdr.code.is_response && tag.hdr.code.resp_size == 4);

  assert((tag.payload & 0xffff0000) == 0, "ptag_get_dma_channel_mask: invalid channel mask %08"PRIx32"\n", tag.payload);

  return tag.payload;
}

uint32_t ptag_get_nominal_clock_rate(enum ptag_clock_id which_clock)
{
  struct
  {
    struct mbox_tag_hdr hdr;
    uint32_t clock_id;
    uint32_t clock_rate;
  } tag = { .hdr = mk_tag_hdr(3, 2, 8), .clock_id = which_clock, .clock_rate = 0 };

  if(!mail(ARM_TO_VC, &tag, sizeof tag))
    panic("ptag_get_nominal_clock_rate: failed to query measured clock rate for clock %d\n", which_clock);

  assert(tag.hdr.code.is_response && tag.hdr.code.resp_size == 8);

  return tag.clock_rate;
}

uint32_t ptag_get_measured_clock_rate(enum ptag_clock_id which_clock)
{
  struct
  {
    struct mbox_tag_hdr hdr;
    uint32_t clock_id;
    uint32_t clock_rate;
  } tag = { .hdr = mk_tag_hdr(3, 0x47, 8), .clock_id = which_clock, .clock_rate = 0 };

  if(!mail(ARM_TO_VC, &tag, sizeof tag))
    panic("ptag_get_measured_clock_rate: failed to query measured clock rate for clock %d\n", which_clock);

  assert(tag.hdr.code.is_response && tag.hdr.code.resp_size == 8);

  return tag.clock_rate;
}
