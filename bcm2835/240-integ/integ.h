#pragma once

#include <stddef.h>
#include <stdint.h>




uint32_t
hash_memory(const void *seed, const void *p, const size_t sz);

#define SEED_SIZE 4




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
  
  // region was corrupted; this is *SET* by the integrity checker
  IRF_Corrupted = 0x10,
  IRF_CorruptedSomewhere = 0x20,
};

// XXX: stack alignment on armv6 is 8. this is mostly so integ_check_coverage doesn't complain.
[[gnu::aligned(8)]]
typedef struct
{
  void *p;
  size_t size;
  uint32_t flags;
  uint32_t cksum;
} IntegRegion;

typedef struct
{
  // XXX: We treat the stack above and below the SP differently; above the SP is IRF_Critical, but
  //      below the SP is IRF_Mutable.
  uintptr_t sp;
  uintptr_t stack_lo;
} StackInteg;

enum {
  // Expect that the low region of the stack will not be mutated
  IHF_ExpectStackCleanup = 0x1,
};
#define MICROSTACK_SIZE 2048
[[gnu::aligned(8)]]
typedef struct
{
  uint32_t self_hash;
  uint32_t lr;
  void (*f)(void *);
  void *f_arg;
  const void *seed;
  uint32_t flags;
  StackInteg stack;
  size_t region_count, region_cap;
  IntegRegion regions[];
} IntegHeader;

enum integ_status
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
  enum integ_status state;
} IntegResult;

