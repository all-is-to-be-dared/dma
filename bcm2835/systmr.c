#include "bcm2835/platform.h"

uint64_t systmr_read_raw(void) {
  uint32_t hi, hi2, lo;
  
  dsb();
  // read hi-lo-hi; if hi2 != hi1, then retry.
  // we speed this up by doing hi-lo-hi-lo-hi instead of hi-lo-hi--hi-lo-hi--...
  // this is to prevent situations where the lower counter overflows after the read from the high
  // counter
  hi2 = systmr->chi;
  do {
    hi = hi2;
    lo = systmr->clo;
    hi2 = systmr->chi;
  } while(hi2 != hi);
  return ((uint64_t)hi) << 32 | (uint64_t)lo;
}
