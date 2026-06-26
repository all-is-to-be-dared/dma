#include <string.h>
#include <inttypes.h>

#include "printf/printf.h"
#include "bcm2835/platform.h"
#include "pup/common.h"
#include "pup/config.h"
#include "pup/trampoline.h"

bool
platform_can_read(void)
{
  return aux_uart_can_read();
}

uint8_t
platform_read(void)
{
  return aux_uart_read();
}

void
platform_write(uint8_t x)
{
  return aux_uart_put(x);
}

uint64_t
platform_time(void)
{
  return systmr_read_raw();
}

enum
{
  DRAM_LIMIT = 0x2000'0000U,
};

static meta_t META;
struct reloc
{
  uint8_t *tgt_start, *tgt_end;
  uint8_t *buf_start, *buf_end;
  uint8_t *stub_start, *stub_end;
  size_t size;
};
static struct reloc reloc;

static uint8_t* feed;

extern uint8_t __prog_start[];
extern uint8_t __prog_end[];

#define min(a, b)           \
  ({                        \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a > _b ? _b : _a;      \
  })
#define max(a, b)           \
  ({                        \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a > _b ? _a : _b;      \
  })

bool
platform_init_meta(meta_t* meta)
{
  uintptr_t img_start, img_end, prog_start, prog_end;
  uintptr_t reloc_tgt_start, reloc_tgt_end, reloc_buf_start, reloc_buf_end;
  uintptr_t stub_start, stub_end;
  size_t reloc_size, stub_size;

  printf(BOOT FUNC("platform_init_meta") "meta { .load_addr=%llx\n", meta->load_addr);
  printf(BOOT FUNC("platform_init_meta") "       .wire_size=%" PRIx32 "\n", meta->wire_size);
  printf(BOOT FUNC("platform_init_meta") "       .mem_size=%" PRIx32 "\n", meta->mem_size);
  printf(BOOT FUNC("platform_init_meta") "       .mem_crc32=%" PRIx32 " }\n", meta->mem_crc32);

  if (meta->load_addr >= DRAM_LIMIT) {
    printf(BOOT FUNC("platform_init_meta") ERROR "load address outside of address space\n");
    return false;
  }
  if (meta->load_addr + (uint64_t)meta->mem_size >= DRAM_LIMIT) {
    printf(BOOT FUNC("platform_init_meta") ERROR "image size overflows address space\n");
    return false;
  }
  memcpy(&META, meta, sizeof META);

  img_start = (uintptr_t)meta->load_addr;
  img_end = (uintptr_t)(meta->load_addr + meta->mem_size);
  prog_start = (uintptr_t)__prog_start;
  prog_end = (uintptr_t)__prog_end;

  printf(
    BOOT FUNC("platform_init_meta") "image = [%" PRIxPTR ",%" PRIxPTR ")\n", img_start, img_end);
  printf(
    BOOT FUNC("platform_init_meta") "prog  = [%" PRIxPTR ",%" PRIxPTR ")\n", prog_start, prog_end);

  reloc_size = 0;

  if (
    //        | PROGRAM ------------ |
    //            | IMAGE ----? ---------? |
    (img_start >= prog_start && img_start < prog_end)
    //        | PROGRAM ------------ |
    //  | IMAGE -----? ------------------? |
    || (img_start < prog_start && img_end > prog_start)) {

    reloc_tgt_start = max(img_start, prog_start);
    reloc_tgt_end = min(img_end, prog_end);
    reloc_size = reloc_tgt_end - reloc_tgt_start;
  }
  stub_size = TRAMPOLINE_END - TRAMPOLINE_START;

  if (reloc_size) {
    reloc_buf_start = max(prog_end, img_end);
    if (reloc_buf_start >= (DRAM_LIMIT - reloc_size - stub_size)) {
      printf(BOOT FUNC("platform_init_meta") ERROR
             "relocation buffer overflows address space (image "
             "size too large)\n");
      return false;
    }
    reloc_buf_end = reloc_buf_start + reloc_size;

    stub_start = (reloc_buf_end + 3) & (-4);
    stub_end = stub_start + stub_size;

    printf(
      BOOT FUNC("platform_init_meta") "relocate [%x,%x] to [%x,%x] (%zx bytes), stub=[%x,%x]\n",
      reloc_tgt_start,
      reloc_tgt_end,
      reloc_buf_start,
      reloc_buf_end,
      reloc_size,
      stub_start,
      stub_end);

    reloc = (struct reloc){
      .tgt_start = (uint8_t*)reloc_tgt_start,
      .tgt_end = (uint8_t*)reloc_tgt_end,
      .buf_start = (uint8_t*)reloc_buf_start,
      .buf_end = (uint8_t*)reloc_buf_end,
      .stub_start = (uint8_t*)stub_start,
      .stub_end = (uint8_t*)stub_end,
      .size = reloc_size,
    };
  } else {
    printf(BOOT FUNC("platform_init_meta") "no relocation\n");
    reloc = (struct reloc){
      .tgt_start = nullptr,
      .tgt_end = nullptr,
      .buf_start = nullptr,
      .buf_end = nullptr,
      .stub_start = nullptr,
      .stub_end = nullptr,
      .size = reloc_size,
    };
  }

  feed = (uint8_t*)(uintptr_t)META.load_addr;

  return true;
}

void
platform_feed(size_t chunk_no, uint8_t* data, size_t len)
{
  size_t cnt, off;

  printf(BOOT "received chunk #%zu (%zuB)\n", chunk_no, len);

  if (reloc.size) {
    if (feed < reloc.tgt_start && (feed + len) > reloc.tgt_start) {
      cnt = reloc.tgt_start - feed;
      memcpy(feed, data, cnt);
      printf(BOOT FUNC("platform_feed") "direct copy to [%p,%p)\n",
             feed,
             feed + cnt);
      len -= cnt;
      data += cnt;
      feed += cnt;
    }
    if (feed >= reloc.tgt_start && feed < reloc.tgt_end) {
      off = feed - reloc.tgt_start;
      cnt = min(reloc.tgt_end - feed, len);
      memcpy(reloc.buf_start + off, data, cnt);
      printf(BOOT FUNC("platform_feed") "buffer copy to [+%zx,+%zx)\n",
             off,
             off + cnt);
      len -= cnt;
      data += cnt;
      feed += cnt;
    }
  }

  if (len) {
    memcpy(feed, data, len);
    printf(BOOT FUNC("platform_feed") "direct copy to [%p,%p)\n", feed, feed + len);
    feed += len;
  }
}

enum
{
  CRC_BLOCKSIZE = 0x4000,
};

static uint32_t
crc_with_heartbeat(uint32_t crc, uint8_t* data, uint8_t* until)
{
  size_t blocksz;
  while (data < until) {
    fsm_heartbeat();
    blocksz = min(until - data, CRC_BLOCKSIZE);
    crc = crc32(crc, data, blocksz);
    data += blocksz;
  }
  return crc;
}

bool
platform_load_image()
{
  uint32_t crc;
  size_t cnt, off;
  bool crc_ok;

  printf(BOOT "platform_load_image()\n");

  crc = CRC_INIT;

  uint8_t* end = (uint8_t*)(uintptr_t)META.load_addr + META.mem_size;

  if (feed != end) {
    printf(BOOT FUNC("platform_load_image") ERROR "feed (%p) did not reach end of image (%p)\n",
           feed,
           end);
    return false;
  }

  feed = (uint8_t*)(uintptr_t)META.load_addr;

  if (reloc.size) {
    if (feed < reloc.tgt_start && end > reloc.tgt_start) {
      cnt = reloc.tgt_start - feed;
      printf(BOOT FUNC("platform_load_image") ""
                                              "CRC [%p,%p)\n",
             feed,
             feed + cnt);
      crc = crc_with_heartbeat(crc, feed, feed + cnt);
      feed += cnt;
    }
    if (feed >= reloc.tgt_start && feed < reloc.tgt_end) {
      off = feed - reloc.tgt_start;
      cnt = min(reloc.tgt_end - feed, end - feed);
      printf(BOOT FUNC("platform_load_image") "CRC indirect [+%zx,+%zx)\n", off, off + cnt);
      crc = crc_with_heartbeat(crc, reloc.buf_start + off, reloc.buf_start + off + cnt);
      feed += cnt;
    }
  }
  if (feed < end) {
    printf(BOOT FUNC("platform_load_image") "CRC [%p,%p)\n", feed, end);
    crc = crc_with_heartbeat(crc, feed, end);
  }

  crc_ok = crc == META.mem_crc32;

  if (!crc_ok) {
    printf(BOOT FUNC("platform_load_image") ERROR "CRC: expected %" PRIx32 " got %" PRIx32 "\n",
           META.mem_crc32,
           crc);
  }

  return crc_ok;
}

[[noreturn]]
static void
trampoline()
{
  uint32_t branch_to;

  if (reloc.size) {
    memcpy(reloc.stub_start, TRAMPOLINE_START, TRAMPOLINE_END - TRAMPOLINE_START);
    branch_to = (uint32_t)reloc.stub_start;
  } else {
    branch_to = (uint32_t)TRAMPOLINE_START;
  }

  printf(BOOT "loaded trampoline, jumping\n");

  aux_uart_flush_tx_fifo();

  register uint32_t r0 asm("r0"), r1 asm("r1"), r2 asm("r2"), r3 asm("r3"), r4 asm("r4");
  r0 = (uint32_t)reloc.tgt_start;
  r1 = (uint32_t)reloc.buf_start;
  r2 = (uint32_t)reloc.size;
  r3 = META.load_addr;
  r4 = branch_to;
  __asm__ volatile("bx r5" : : "r"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4));

  __builtin_unreachable();
}

void
main(void)
{
  uint32_t r;
  __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r" (r));
  r |= (1 << 11) | (1 << 12);
  __asm__ volatile ("mcr p15, 0, %0, c1, c0, 0" : : "r" (r));
  
  gpio_pin_set_function(14, FSEL_ALT5);
  gpio_pin_set_function(15, FSEL_ALT5);

  aux_uart_init(BAUD_RATE, 250'000'000);

  fsm_download(&config);

  trampoline();

  // printf("FSM EXITED\n");
  // printf("DONE!!!\n");
  // aux_uart_flush_tx_fifo();
}
