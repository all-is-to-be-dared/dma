#pragma once

#include "pup/common.h"

enum
{
  BAUD_RATE = 1152000,
};

static config_t config = {
  .baud = BAUD_RATE,
  .symbol_timeout = 4,
  .resend_timeout = 80'000,
  .reset_timeout = 300'000,
  .heartbeat_interval = 100'000,
};


