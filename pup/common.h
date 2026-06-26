#pragma once

#include <stdint.h>
#include <stddef.h>

#define HOST "\x1b[35mHOST\x1b[0m: "
#define BOOT "\x1b[33mBOOT\x1b[0m: "
#define FUNC(s) "\x1b[32m" s "\x1b[0m: "
#define ERROR "\x1b[31mERROR\x1b[0m: "

// payload: meta_t
enum compress_type : uint32_t {
  COMPRESS_NONE,
  COMPRESS_XZ,
  NUM_COMPRESS_TYPES,
};
static_assert(sizeof(enum compress_type) == 4);
typedef struct
{
  uint64_t load_addr;
  // number of bytes that will be sent over the wire
  uint32_t wire_size;
  // number of bytes that the final image occupies in memory
  uint32_t mem_size;
  uint32_t mem_crc32;
  enum compress_type compress;
} meta_t;
#define HOST_POLL "POLL"
#define HOST_CHNK "CHNK"

// payload
typedef struct
{
  uint32_t num;
} chunkreq_t;
#define DEV_RQCH "RQCH"
#define DEV_BEAT "BEAT"
typedef struct
{
  uint8_t is_ok;
} boot_t;
#define DEV_BOOT "BOOT"

enum
{
  ASC_NUL = 0x00,
  ASC_SOH = 0x01,
  ASC_STX = 0x02,
  ASC_ETX = 0x03,
  ASC_EOT = 0x04,

  ASC_ENQ = 0x05,
  ASC_ACK = 0x06,
  ASC_BEL = 0x07,

  ASC_DLE = 0x10,
  ASC_NAK = 0x15,
  ASC_SYN = 0x16,
  ASC_ETB = 0x17,
  ASC_CAN = 0x18,

  ASC_FS = 0x1c,
  ASC_GS = 0x1d,
  ASC_RS = 0x1e,
  ASC_US = 0x1f,
};




bool
platform_can_read(void);

uint8_t
platform_read(void);

void
platform_write(uint8_t);

uint64_t
platform_time(void);

bool
platform_init_meta(meta_t* meta);

bool
platform_feed(size_t chunk_no, uint8_t* data, size_t len);

bool
platform_load_image(void);

bool
platform_marshal(size_t chunk_no, uint8_t** data, uint16_t* len);




typedef struct
{
  union
  {
    struct
    {
      uint16_t iden;
      uint16_t plen;
      char type[4];
      uint16_t hdr_fcs;
    };
    uint8_t raw_hdr[10];
  };
  union
  {
    struct
    {
      uint8_t body[(1u << 16)];
      uint16_t body_fcs;
    };
    uint8_t raw_body[(1u << 16) + 2u];
  };
} mframe_t;




enum
{
  CRC_INIT = 0
};
uint32_t
crc32(uint32_t crc, const uint8_t* p, size_t size);




// CRC16-based frame check sequence, pulled from §C.2 of RFC1662.
enum
{
  FCS_INIT = 0xffff,
  FCS_OK = 0xf0b8
};
uint16_t
fcs16(uint16_t fcs, const uint8_t* p, size_t size);




static inline uint16_t
fsm_write_fcs(uint8_t x, uint16_t fcs)
{
  platform_write(x);
  return fcs16(fcs, &x, 1);
}

static inline void
fsm_write16(uint16_t x)
{
  platform_write(x & 0xff);
  platform_write(x >> 8);
}

static inline uint16_t
fsm_write16_fcs(uint16_t x, uint16_t fcs)
{
  fcs = fsm_write_fcs(x & 0xff, fcs);
  fcs = fsm_write_fcs(x >> 8, fcs);
  return fcs;
}




typedef struct
{
  /* baud rate at which the FSM is transmitting and receiving data */
  uint32_t baud;
  /* time, in symbol-transmit-durations, to wait per symbol */
  uint32_t symbol_timeout;
  /* time, in symbol-transmit-durations, to wait before resending a frame */
  uint32_t resend_timeout;
  /* time, in symbol-transmit-durations, to wait before resetting the FSM */
  uint32_t reset_timeout;
  /* time, in symbol-transmit-durations, to wait between heartbeats during loading */
  uint32_t heartbeat_interval;
} config_t;

static inline uint64_t
config_symbol_timeout_us(config_t* config)
{
  // (time to send 1 symbol) * symbol_timeout
  // time to send 1 symbol [in μs] = 100'000/(config->baud / 10)

  // [B] * [us/s] / [B/s] -> [B] [us/s] [s/B] -> us

  uint64_t baseline = (((uint64_t)config->symbol_timeout) * 1'000'000) / (config->baud / 10);

  // For high baud rates, the quotient approximation goes to zero, so we force it to be nonzero
  if (!baseline)
    baseline = 1;

  return baseline;
}

static inline uint64_t
config_resend_timeout_us(config_t* config)
{
  uint64_t baseline = (((uint64_t)config->resend_timeout) * 1'000'000) / (config->baud / 10);
  if (!baseline)
    baseline = 1;
  return baseline;
}

static inline uint64_t
config_reset_timeout_us(config_t* config)
{
  uint64_t baseline = (((uint64_t)config->reset_timeout) * 1'000'000) / (config->baud / 10);
  if (!baseline)
    baseline = 1;
  return baseline;
}




void
fsm_download(config_t* config);




enum fsm_upload_status
{
  UPLOAD_OK,
  UPLOAD_TIMEOUT,
  UPLOAD_FAILED,
};
struct uploader_hooks
{
  void (*on_poll_acked)();
  void (*on_sent_chunk)(uint32_t);
  void (*on_heartbeat)();
  void (*on_passthru)(uint8_t);
};
enum fsm_upload_status
fsm_upload(config_t* config,
           meta_t* meta,
           const uint8_t* data,
           uint8_t retries,
           struct uploader_hooks hooks);

void
fsm_heartbeat();




void*
xz_malloc_stub(size_t size);

void
xz_free_stub(void *p);
