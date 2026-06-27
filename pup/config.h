#pragma once

#include "pup/common.h"

#define BAUD_RATE 3'000'000
// BAUD_RATE = 3125000,
// BAUD_RATE = 3000000,
// BAUD_RATE = 1562500,
// BAUD_RATE = 1152000,
// BAUD_RATE = 1500000,




static config_t config = {
  .baud = BAUD_RATE,
#if BAUD_RATE < 1'500'000
  .symbol_timeout = 1'000,
  .resend_timeout = 80'000,
  .reset_timeout = 300'000,
  .heartbeat_interval = 100'000,
#else
  .symbol_timeout = 1'000,
  .resend_timeout = 70'000,
  .reset_timeout = 500'000,
  .heartbeat_interval = 100'000,
#endif
};


