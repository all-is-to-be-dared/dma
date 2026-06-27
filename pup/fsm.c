#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <inttypes.h>
#include "pup/common.h"

#if !NO_DOWNLOAD
#include "printf/printf.h"
#include "bcm2835/platform.h"
#define SIDE BOOT
#elif !NO_UPLOAD
#include "pup/host.h"
#define printf(...) host_printf(DBG_FULL, __VA_ARGS__)
#define SIDE HOST
#endif




static void (*passthru)(uint8_t) = nullptr;




/* ------------------------------------------------------------------------------------------------
   TYPES */




enum fsm_timer
{
  TIMER_RESET = 0,
  TIMER_RESEND = 1,
  TIMER_SYMBOL = 2,
  NUM_TIMERS,
};
const char *TIMER_NAMES[] = {
  [TIMER_RESET] = "TIMER_RESET",
  [TIMER_RESEND] = "TIMER_RESEND",
  [TIMER_SYMBOL] = "TIMER_SYMBOL",
};


typedef struct
{
  uint64_t last;
  uint64_t period;
  bool active;
} timer_t;

typedef struct
{
  timer_t timers[NUM_TIMERS];
  uint16_t iden;
} fsm_t;

enum fsm_status
{
  FSM_OK = 0,
  FSM_RESET = 1,
  FSM_RESEND = 2,
};
const char *FSM_STATUS_NAMES[] = {
  [FSM_OK] = "FSM_OK",
  [FSM_RESET] = "FSM_RESET",
  [FSM_RESEND] = "FSM_RESEND",
};

static fsm_t fsm;
static mframe_t framebuf;




/* ------------------------------------------------------------------------------------------------
   FSM LIFECYCLE */



void
fsm_init(void)
{
  for (int i = 0; i < NUM_TIMERS; ++i)
    fsm.timers[i] = (timer_t){ .last = 0, .period = 0, .active = false };
}




/* ------------------------------------------------------------------------------------------------
   TIMER MANAGEMENT */




static inline bool
timer_triggered(timer_t* timer, uint64_t now)
{
  return timer->active && (timer->last + timer->period) <= now;
}




static inline bool
fsm_timer_tick(enum fsm_timer timer, uint64_t now)
{
  if (timer_triggered(fsm.timers + timer, now)) {
    fsm.timers[timer].last = now;
    return 1;
  }
  return 0;
}




static inline void
fsm_timer_clear(enum fsm_timer timer, uint64_t now)
{
  fsm.timers[timer].last = now;
  fsm.timers[timer].active = true;
}




/* ------------------------------------------------------------------------------------------------
   FRAME SEND/RECV */




enum fsm_status
fsm_recv(mframe_t* frame)
{
  size_t i;
  uint32_t rem_body, body_size;
  uint8_t c, *body;

#if !NO_DOWNLOAD
  dsb();
  if (aux_uart->stat & (1 << 4)) {
    printf(BOOT "\x1b[31mreceiver overrun\x1b[0m\n");
    (void)aux_uart->lsr;
  }
  dsb();
#endif


wait_for_soh:
  while (1) {
    if (fsm_timer_tick(TIMER_RESET, platform_time()))
      return FSM_RESET;
    if (fsm_timer_tick(TIMER_RESEND, platform_time()))
      return FSM_RESEND;

    if (platform_can_read()) {
      c = platform_read();
      if (c == ASC_SOH)
        break;
      else if (passthru)
        passthru(c);
    }
  }

#define wait_for(x, lbl)                               \
  fsm_timer_clear(TIMER_SYMBOL, platform_time());      \
  while (!platform_can_read())                         \
    if (fsm_timer_tick(TIMER_SYMBOL, platform_time())) \
      goto lbl;                                        \
  if (platform_read() != (x))                          \
    goto lbl;

  // Receive header
  for (i = 0; i < sizeof frame->raw_hdr; ++i) {
    fsm_timer_clear(TIMER_SYMBOL, platform_time());
    while (!platform_can_read()) {
      if (fsm_timer_tick(TIMER_SYMBOL, platform_time()))
        goto wait_for_soh;
    }
    frame->raw_hdr[i] = platform_read();
  }

  // Slightly nonstandard use of CRC : the FCS bytes at the end of the buffer force the result to
  // be a certain value (0xf0b8), in our case.
  if (FCS_OK != fcs16(FCS_INIT, frame->raw_hdr, sizeof frame->raw_hdr))
    goto wait_for_soh;

  // Receive STX
  wait_for(ASC_STX, wait_for_soh);

  // Receive body + FCS
  rem_body = body_size = 2u + (uint32_t)frame->plen;
  body = frame->raw_body;
  while (rem_body--) {
    fsm_timer_clear(TIMER_SYMBOL, platform_time());
    while (!platform_can_read()) {
      if (fsm_timer_tick(TIMER_SYMBOL, platform_time()))
        goto wait_for_soh;
    }
    *body++ = platform_read();
  }

  wait_for(ASC_ETX, wait_for_soh);

  wait_for(ASC_EOT, wait_for_soh);

  if (FCS_OK != fcs16(FCS_INIT, frame->raw_body, body_size))
    goto wait_for_soh;

  return FSM_OK;
}




enum fsm_status
fsm_recv_ack(uint16_t iden)
{
  size_t i;
  union
  {
    struct
    {
      uint16_t iden;
      uint16_t fcs;
    };
    uint8_t raw_frame[4];
  } ack;
  uint8_t c, *body;

#if !NO_DOWNLOAD
  dsb();
  if (aux_uart->stat & (1 << 4)) {
    printf(BOOT "\x1b[31mreceiver overrun\x1b[0m\n");
    (void)aux_uart->lsr;
    aux_uart->iir |= 2;
  }
  dsb();
#endif

wait_for_ack:
  while (1) {
    if (fsm_timer_tick(TIMER_RESET, platform_time()))
      return FSM_RESET;
    if (fsm_timer_tick(TIMER_RESEND, platform_time()))
      return FSM_RESEND;

    if (platform_can_read()) {
      c = platform_read();
      if (c == ASC_ACK)
        break;
      else if (passthru)
        passthru(c);
    }
  }

  body = ack.raw_frame;
  for (i = 0; i < sizeof ack; ++i) {
    fsm_timer_clear(TIMER_SYMBOL, platform_time());
    while (!platform_can_read()) {
      if (fsm_timer_tick(TIMER_SYMBOL, platform_time()))
        goto wait_for_ack;
    }
    *body++ = platform_read();
  }

  wait_for(ASC_EOT, wait_for_ack);

  if (FCS_OK != fcs16(FCS_INIT, ack.raw_frame, sizeof ack.raw_frame))
    goto wait_for_ack;

  if (ack.iden != iden)
    goto wait_for_ack;

  return FSM_OK;
}




void
fsm_send(const char* type, const uint8_t* payload, uint16_t payload_len)
{
  uint16_t fcs;
  size_t i;

  platform_write(ASC_SOH);
  fcs = FCS_INIT;
  fcs = fsm_write16_fcs(fsm.iden, fcs);
  fcs = fsm_write16_fcs(payload_len, fcs);
  for (i = 0; i < 4; i++)
    fcs = fsm_write_fcs(type[i], fcs);
  fsm_write16(fcs ^ 0xffff);

  platform_write(ASC_STX);
  fcs = FCS_INIT;
  for (i = 0; i < payload_len; i++)
    fcs = fsm_write_fcs(payload[i], fcs);
  // while (payload_len--) {
  //   fcs = fsm_write_fcs(*payload++, fcs);
  // }
  fsm_write16(fcs ^ 0xffff);

  platform_write(ASC_ETX);
  platform_write(ASC_EOT);
}




void
fsm_send_ack(uint16_t iden)
{
  uint16_t fcs;

  platform_write(ASC_ACK);
  fcs = FCS_INIT;
  fcs = fsm_write16_fcs(iden, fcs);
  fsm_write16(fcs ^ 0xffff);

  platform_write(ASC_EOT);
}




/* ------------------------------------------------------------------------------------------------
   DOWNLOADER MACHINERY */




#if !NO_DOWNLOAD




void
fsm_download(config_t* config)
{
  uint32_t chunk_count, chunk_num;
  meta_t meta;
  chunkreq_t chunk_req;
  boot_t boot;
  enum fsm_status r;

  fsm.timers[TIMER_RESET].period = config_reset_timeout_us(config);
  fsm.timers[TIMER_RESEND].period = config_resend_timeout_us(config);
  fsm.timers[TIMER_SYMBOL].period = config_symbol_timeout_us(config);
  fsm.timers[TIMER_SYMBOL].active = true;

reset:
  fsm.iden = 0;
  fsm.timers[TIMER_RESET].active = false;
  fsm.timers[TIMER_RESEND].active = false;

  // printf(BOOT "FSM DOWNLOAD INIT\n");

  while (1) {
    if (FSM_OK == fsm_recv(&framebuf)) {
      // printf(BOOT "received frame (INIT), TYPE=%.4s IDEN=%hu PLEN=%hu\n",
      //        framebuf.type,
      //        framebuf.iden,
      //        framebuf.plen);
      if (!memcmp(framebuf.type, HOST_POLL, 4) && framebuf.iden == 0 &&
          framebuf.plen == sizeof meta) {
        memcpy(&meta, framebuf.raw_body, sizeof meta);
        break;
      }
    }
  }

  if (!platform_init_meta(&meta))
    goto reset;

  // received HOST_POLL; we're making forward progress, so we init the RESET timer
  fsm_timer_clear(TIMER_RESET, platform_time());

  // Acknowledge the HOST_POLL
  fsm_send_ack(0);
  // fsm_send_ack(0);
  // fsm_send_ack(0);

  // Get all the chunks
  chunk_count = (meta.wire_size + 0xfffe) / 0xffff;
  chunk_num = 0;
  while (chunk_num < chunk_count) {
    chunk_req = (chunkreq_t){ .num = chunk_num };
    while (1) {
      fsm_send(DEV_RQCH, (void*)&chunk_req, sizeof chunk_req);
      fsm_timer_clear(TIMER_RESEND, platform_time());

      // uint64_t t0 = platform_time();
      r = fsm_recv(&framebuf);
      // uint64_t t1 = platform_time();
      // printf(BOOT "wait for CHNK for %lluμs, %s\n", t1-t0, FSM_STATUS_NAMES[r]);
      // printf(BOOT "RESET=%llu/%llu/%d now=%llu\n",
      //        fsm.timers[TIMER_RESET].last,
      //        fsm.timers[TIMER_RESET].period,
      //        fsm.timers[TIMER_RESET].active,
      //        platform_time());
      // printf(BOOT "received frame (CHNK), TYPE=%.4s IDEN=%hu PLEN=%hu\n",
      //        framebuf.type,
      //        framebuf.iden,
      //        framebuf.plen);
      if (r == FSM_RESET)
        goto reset;
      if (r == FSM_RESEND)
        continue;
      if (!memcmp(framebuf.type, HOST_CHNK, 4) && framebuf.iden == fsm.iden)
        break;
    }

    // fsm.timers[TIMER_RESET].active = false;
    if(!platform_feed(chunk_num, framebuf.body, framebuf.plen)) {
      goto reset;
    }
    fsm_timer_clear(TIMER_RESET, platform_time());
    fsm.iden += 1;

    chunk_num += 1;
  }

  // will call out to fsm_heartbeat() as needed
  boot.is_ok = platform_load_image();

  fsm_timer_clear(TIMER_RESET, platform_time());
  while (1) {
    fsm_send(DEV_BOOT, (void*)&boot, sizeof boot);
    fsm_timer_clear(TIMER_RESEND, platform_time());

    r = fsm_recv_ack(fsm.iden);
    if (r == FSM_RESET)
      goto reset;
    if (r == FSM_RESEND)
      continue;
    if (r == FSM_OK) {
      printf(BOOT "received ACK for BOOT\n");
      break;
    }
  }

  return;
}




void
fsm_heartbeat(void)
{
  fsm_send(DEV_BEAT, NULL, 0);
}




#endif




/* ------------------------------------------------------------------------------------------------
   UPLOADER MACHINERY */




#if !NO_UPLOAD




enum fsm_upload_status
fsm_upload(config_t* config,
           meta_t* meta,
           const uint8_t* data,
           uint8_t retries_,
           struct uploader_hooks hooks)
{
  enum fsm_status r;
  chunkreq_t chunk_req;
  boot_t boot;
  uint8_t* marshal_data;
  uint16_t retries, marshal_len;
  uint32_t last_chunk_no;
  size_t i;

  fsm.timers[TIMER_RESET].period = config_reset_timeout_us(config);
  fsm.timers[TIMER_RESEND].period = config_resend_timeout_us(config);
  fsm.timers[TIMER_SYMBOL].period = config_symbol_timeout_us(config);
  fsm.timers[TIMER_SYMBOL].active = true;

  for(i = 0;i < NUM_TIMERS;i++) {
    host_printf(DBG_MIN, HOST FUNC("config") "%s period: %lluμs\n", TIMER_NAMES[i], fsm.timers[i].period);
  }

  retries = retries_ + 1;
  passthru = hooks.on_passthru;

reset:
  if (!(retries--))
    return UPLOAD_TIMEOUT;
  if(retries < retries_)
    host_printf(DBG_MIN, HOST "Polling device (retry %d/%d)\n", retries_ - retries, retries_);
  fsm.iden = 0;
  fsm.timers[TIMER_RESEND].active = false;
  // Need to init TIMER_RESET so that if the device doesn't respond, we can still stop after a
  // certain point.
  fsm_timer_clear(TIMER_RESET, platform_time());
  // fsm.timers[TIMER_RESET].active = false;

  while (1) {
    fsm_send(HOST_POLL, (void*)meta, sizeof *meta);
    fsm_timer_clear(TIMER_RESEND, platform_time());

    r = fsm_recv_ack(0);
    if (r == FSM_RESET)
      goto reset;
    if (r == FSM_OK)
      break;
  }

  hooks.on_poll_acked();

  fsm_timer_clear(TIMER_RESET, platform_time());
  fsm.timers[TIMER_RESEND].active = false;

  last_chunk_no = 0;


  while (1) {
    r = fsm_recv(&framebuf);
    if (r == FSM_RESET) {
      goto reset;
    }

    // printf(HOST "received: %.4s\n", framebuf.type);

    if (!memcmp(DEV_BEAT, framebuf.type, 4) && framebuf.plen == 0) {
      hooks.on_all_chunks();
      break;
    }
    if (!memcmp(DEV_BOOT, framebuf.type, 4) && framebuf.plen == sizeof boot) {
      hooks.on_all_chunks();
      goto received_boot;
    }

    if (!memcmp(DEV_RQCH, framebuf.type, 4) && framebuf.plen == sizeof
                                               chunk_req) {
      memcpy(&chunk_req, framebuf.body, sizeof chunk_req);

      if (!platform_marshal(chunk_req.num, &marshal_data, &marshal_len))
        continue;

      host_printf(DBG_FULL, HOST "received chunk request (%" PRIi32 "), sending (%huB)\n", chunk_req.num, marshal_len);

      uint16_t save_iden = fsm.iden;
      fsm.iden = framebuf.iden;
      fsm_send(HOST_CHNK, marshal_data, marshal_len);
      fsm.iden = save_iden;
      hooks.on_sent_chunk(chunk_req.num);

      if (last_chunk_no != chunk_req.num)
        fsm_timer_clear(TIMER_RESET, platform_time());
      last_chunk_no = chunk_req.num;
    }
  }

  printf(HOST "started receiving device heartbeat\n");

  hooks.on_heartbeat();
  fsm_timer_clear(TIMER_RESET, platform_time());

  while (1) {
    r = fsm_recv(&framebuf);
    if (r == FSM_RESET) {
      // printf(HOST "timed out waiting for BEAT/BOOT\n");
      goto reset;
    }

    // printf(HOST "received: %.4s\n", framebuf.type);

    if (!memcmp(DEV_BEAT, framebuf.type, 4) && framebuf.plen == 0) {
      hooks.on_heartbeat();
      fsm_timer_clear(TIMER_RESET, platform_time());
    }

    if (!memcmp(DEV_BOOT, framebuf.type, 4) && framebuf.plen == sizeof boot) {
      goto received_boot;
    }
  }

received_boot:
  // host_printf(HOST "received BOOT frame, ok=%d\n", boot.is_ok);
  fsm_send_ack(framebuf.iden);
  memcpy(&boot, framebuf.body, sizeof boot);
  return boot.is_ok ? UPLOAD_OK : UPLOAD_FAILED;
}




#endif
