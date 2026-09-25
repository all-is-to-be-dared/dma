#pragma once

#include <stddef.h>
#include <stdint.h>

enum {
  // expected to change
  IRF_Mutable = 0x1,
  // region does not contain useful data
  // 
  // IRF_Purgable is not compatible with IRF_Mutable and IRF_Critical
  IRF_Purgable = 0x2,
  // if this region has been overwritten, essential program function is compromised and integrity
  // checking cannot continue
  IRF_Critical = 0x4,
  // region contains program stack; the IntegRegion should be ignored and instead processed using
  // the StackInteg in the IntegRegistry.
  IRF_Stack = 0x8,
};

typedef struct
{
  void *p;
  size_t size;
  uint32_t flags;
  uint32_t cksum;
} IntegRegion;

typedef struct
{
  uintptr_t sp;
  // XXX: We treat the stack above and below the SP differently; above the SP is IRF_Critical, but
  //      below the SP is IRF_Purgable. Note also that the 'high' stack may be discontinuous, as
  //      there may be IRF_Mutable regions in stack memory; the 'hash_hi' value is calculated by
  //      skipping over these.
  uintptr_t stack_hi, stack_lo;
  uint32_t hash_hi, hash_lo;
} StackInteg;

typedef struct
{
  uint32_t fcs32;
  StackInteg stack;
  size_t region_count, region_cap;
  IntegRegion regions[];
} IntegRegistry;

uint32_t
memory_hash(const void *seed_ptr, const void *p, const size_t sz);
#define SEED_SIZE 4

enum integ_state
{
  // no memory was found to be corrupted
  IS_Clean = 0,
  // memory was corrupted, and integrity checker can narrow down the location
  IS_AbCon = 1,
  // memory was corrupted, but program is no longer in good enough shape to narrow down the location
  IS_AbEnd = 2,
};
typedef struct
{
  enum integ_state state;
  size_t bad_region_idx;
  size_t additional_regions_required;
} IntegResult;

