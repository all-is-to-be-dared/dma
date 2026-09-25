#pragma once

#include <stdint.h>

struct ptag_mem_region
{
  uint32_t base;
  uint32_t size;
};

struct ptag_mem_region
ptag_get_arm_memory(void);

struct ptag_mem_region
ptag_get_vc_memory(void);

uint16_t
ptag_get_dma_channel_mask(void);

enum ptag_clock_id
{
  PCID_RESERVED = 0,
  PCID_EMMC = 1,
  PCID_UART = 2,
  PCID_ARM = 3,
  PCID_CORE = 4,
  PCID_V3D = 5,
  PCID_H264 = 6,
  PCID_ISP = 7,
  PCID_SDRAM = 8,
  PCID_PIXEL = 9,
  PCID_PWM = 10,
  PCID_HEVC = 11,
  PCID_EMMC2 = 12,
  PCID_M2MC = 13,
  PCID_PIXEL_BVB = 14,
};

uint32_t
ptag_get_measured_clock_rate(enum ptag_clock_id which_clock);

uint32_t
ptag_get_nominal_clock_rate(enum ptag_clock_id which_clock);

void
ptag_get_clock_rate_limits(enum ptag_clock_id which_clock, uint32_t *min, uint32_t *max);
