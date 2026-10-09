#pragma once

#include <stddef.h>
#include <stdint.h>




uint32_t
memory_hash(const void *seed_ptr, const void *p, const size_t sz);

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



/*
  xoroshiro64**

  result = [ (s0 * 0x9e3779bb) <<< 5 ] * 5

  s1' = s1 ^ s0
  s0' = (s0 <<< 26) ^ s1' ^ s1' << 9
  s1'' = (s1' <<< 13)

  mul r0, s0, r_GR       @ s0 * 0x9e3779bb   -> T

  xor s1, s1, s0         @ s1 ^ s0           -> s1
  xor r1, s1, s1, lsl #9 @ s1 ^ s1 << 9
  xor s0, r1, s0, ror #6 @ ... ^ (s0 <<< 26) -> s0
  ror s1, s1, #19        @ s1 <<< 13         -> s1

  @ for xoroshiro64*, can just drop these last two lines
  mov r0, r0, ror #27    @ T <<< 5
  add r0, r0, r0, lsl #2 @ ... * 5           -> result
*/

/*
  xoshiro128++

  result = [ (s0 + s3) <<< 7 ] + s0

  s2' =  s2 ^ s0 ^ (s1 << 9)
  s3' = (s3 ^ s1) <<< 11
  s0' =  s0 ^ s3'
  s1' =  s1 ^ s2'

  add r0, s0, s3          @ s0 + s3
  add r0, s0, r0, ror #25 @ s0 + (... <<< 7)

  xor s2, s2, s0
  xor s2, s2, s1, lsl #9
  xor s3, s3, s1
  ror s3, s3, #21
  xor s0, s0, s3
  xor s1, s1, s2
*/

/*
  xoshiro128**

  result = [ (s1 * 5) <<< 7 ] * 9

  s2' =  s2 ^ s0 ^ (s1 << 9)
  s3' = (s3 ^ s1) <<< 11
  s1' =  s1 ^ s2
  s0' =  s0 ^ s3

  add r0, s1, s1, lsl #2 @ s1*5
  ror r0, r0, #25        @ ... <<< 7
  add r0, r0, r0, lsl #3 @ ... * 9
 */

/*
  xorshift32

  s = s ^ (s << 13)
  s = s ^ (s >> 17)
  s = s ^ (s << 5)
*/
