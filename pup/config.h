#pragma once

#include "pup/common.h"

enum
{
  BAUD_RATE = 3125000,
};

static config_t config = {
  .baud = BAUD_RATE,
  // 
  .symbol_timeout = 1'000,
  .resend_timeout = 80'000,
  .reset_timeout = 300'000,
  .heartbeat_interval = 100'000,
};


