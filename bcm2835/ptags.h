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

uint32_t
ptag_get_dma_channel_mask(void);
