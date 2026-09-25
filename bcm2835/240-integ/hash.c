#include <string.h>
#include "integ.h"

#if __ARM_ARCH != 6
#define HASH_HOSTED 1
#endif

#if !defined(HASH_HOSTED)
#include "generic/printf.h"
#include "generic/assert.h"
#include "generic/math.h"
#else
#include <assert.h>
#define min(a, b)           \
  ({                        \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a > _b ? _b : _a;      \
  })
#endif

// Some primes
enum
{
  PRIME_1 = 2654435761U,
  PRIME_2 = 2246822519U,
  PRIME_3 = 3266489917U,
  PRIME_4 = 668265263U,
  PRIME_5 = 374761393U,
};

/// Rotate `x` to the left by `r mod 32` bits. `r` must be a constant value (this is enforced by
/// a compile-time assertion).
#define RotateLeft(x, r)                                                                           \
  ({                                                                                               \
    static_assert(__builtin_constant_p(r), "RotateLeft rotation value must be constant, got " #r); \
    __typeof__(x) t = x;                                                                           \
    (t << r) | (t >> (-(r & 31) & 31));                                                            \
  })

/// Hash a segment of memory `[p,p+sz)`.
///
/// CRITICAL: Final hash value depends on the address of the data.
///
/// XXX: Designed to run at DRAM speed on the BCM2835, it's essentially a three-lane restriping of
///      Yann Collet's XXH32 algorithm (https://github.com/cyan4973/xxhash).
uint32_t
memory_hash(const void *seed_ptr, const void *p, const size_t sz)
{
  // algorithm: three-lane version of XXH32, with a preload stage for alignment
  // note that the preload stage makes the hash value dependent on the alignment-offset (mod 16) of
  // the data address
  //
  // this is fine for our particular case (because we're checking memory regions against their past)
  // selves, but make this unsuitable as a generic hash function.
  //
  // s0 = seed + P1 + P2
  // s1 = seed + P2
  // s2 = seed
  //
  // head:
  //  for w : 1 (uxt) do H = [ (H + w * P5) rol 11 ] * P1
  //  for w : 4       do H = [ (H + w * P3) rol 17 ] * P4
  //
  // accumulate step:
  //  sN = [ (sN + wN * P2) rol 13 ] * P1
  //
  // lane combination:
  //  (large) H = size + (s0 rol 1) + (s1 rol 7) + (s2 rol 12)
  //  (small) H = size + seed + P5
  //
  // tail:
  //  for w : 4       do H = [ (H + w * P3) rol 17 ] * P4
  //  for w : 1 (uxt) do H = [ (H + w * P5) rol 11 ] * P1
  //
  // avalanche:
  //  H = H xor (H >> 15)
  //  H = H * P2
  //  H = H xor (H >> 13)
  //  H = H * p3
  //  H = H xor (H >> 16)
  //
  // Note also that the step size has changed: we now use a step size of 32B instead of 16B.
  //
  // SMHasher results:
  //  - slightly worse than XXH32

  uint32_t H, seed;
#if HASH_HOSTED
  register uint32_t s0, s1, s2;
  register const uint8_t *data;
#elif __ARM_ARCH == 6
  register uint32_t s0 asm("r0"), s1 asm("r1"), s2 asm("r2");
  register const uint8_t *data asm("r3");
#endif
  const uint8_t *start_word, *start_block, *stop_block, *stop_word, *stop;

  seed = seed_ptr ? *(const uint32_t *)seed_ptr : 0;

  s0 = seed + PRIME_1 + PRIME_2;
  s1 = seed + PRIME_2;
  s2 = seed;

  data = p;

  stop = data + sz;
  start_word = __builtin_assume_aligned((const void *)(((uintptr_t)data + 3) & ~3), 4);
  start_block = __builtin_assume_aligned((const void *)(((uintptr_t)data + 31) & ~31), 32);
  stop_word = __builtin_assume_aligned((const void *)((uintptr_t)stop & ~3), 4);
  stop_block = __builtin_assume_aligned((const void *)((uintptr_t)stop & ~31), 32);

  start_word = min(stop_word, start_word);
  start_block = min(stop_block, start_block);

  data = start_block;

#if !defined(HASH_HOSTED) && __ARM_ARCH == 6
  if ((stop_block - start_block) >= 64) {
    register uint32_t prime1 asm("r4") = PRIME_1, prime2 asm("r5") = PRIME_2;
    register const uint8_t *_stop_block asm("r14") = stop_block;
    __asm__(
      "push {r4-r12, r14}\n"
      "adr r7, 3f\n"
      "str sp, [r7]\n"
      "mov r12, r4\n"
      "mov r13, r5\n"

      "ldmia %[dat]!, {r4-r11}\n"
      "mla %[s0], r4, r13, %[s0]\n"
      "mla %[s1], r5, r13, %[s1]\n"
      "mla %[s2], r6, r13, %[s2]\n"
      "ror %[s0], %[s0], #19\n"
      "ror %[s1], %[s1], #19\n"
      "ror %[s2], %[s2], #19\n"
      "mul %[s0], %[s0], r12\n"
      "mul %[s1], %[s1], r12\n"
      "mul %[s2], %[s2], r12\n"

      "mla %[s0], r7, r13, %[s0]\n"
      "mla %[s1], r8, r13, %[s1]\n"
      "mla %[s2], r9, r13, %[s2]\n"
      "ror %[s0], %[s0], #19\n"
      "ror %[s1], %[s1], #19\n"
      "ror %[s2], %[s2], #19\n"
      "mul %[s0], %[s0], r12\n"
      "mul %[s1], %[s1], r12\n"

      "mla %[s0], r10, r13, %[s0]\n"
      "mla %[s1], r11, r13, %[s1]\n"
      "ror %[s0], %[s0], #19\n"
      "ror %[s1], %[s1], #19\n"

      "2:\n"
      "pld [%[dat], #64]\n"
      "ldmia %[dat]!, {r4-r11}\n"
      
      "mul %[s0], %[s0], r12\n"
      "mul %[s1], %[s1], r12\n"
      "mul %[s2], %[s2], r12\n"
      "mla %[s0], r4, r13, %[s0]\n"
      "mla %[s1], r5, r13, %[s1]\n"
      "mla %[s2], r6, r13, %[s2]\n"
      "ror %[s0], %[s0], #19\n"
      "ror %[s1], %[s1], #19\n"
      "ror %[s2], %[s2], #19\n"

      "mul %[s0], %[s0], r12\n"
      "mul %[s1], %[s1], r12\n"
      "mul %[s2], %[s2], r12\n"
      "mla %[s0], r7, r13, %[s0]\n"
      "mla %[s1], r8, r13, %[s1]\n"
      "mla %[s2], r9, r13, %[s2]\n"
      "ror %[s0], %[s0], #19\n"
      "ror %[s1], %[s1], #19\n"
      "ror %[s2], %[s2], #19\n"

      "mul %[s0], %[s0], r12\n"
      "mul %[s1], %[s1], r12\n"
      "mla %[s0], r10, r13, %[s0]\n"
      "mla %[s1], r11, r13, %[s1]\n"
      "cmp %[dat], r14\n"
      "ror %[s0], %[s0], #19\n"
      "ror %[s1], %[s1], #19\n"

      "bne 2b\n"

      "mul %[s0], %[s0], r12\n"
      "mul %[s1], %[s1], r12\n"
      "mul %[s2], %[s2], r12\n"

      "mov r4, r12;\n"
      "mov r5, r13;\n"
      "adr r7, 3f;\n"
      "ldr sp, [r7];\n"
      "b 4f;\n"
      "3: .word 0\n"
      "4:\n"
      "pop {r4-r12, r14};\n"

      : [dat] "+r"(data),
        [s0] "+r"(s0),
        [s1] "+r"(s1),
        [s2] "+r"(s2)
      : "r"(prime1),
        "r"(prime2),
        "r"(_stop_block)
      : "memory"
    );
  } else
#endif
  {
    uint32_t buf[8];
    while (data < stop_block) {
      memcpy(buf, data, 32);
      s0 = RotateLeft(s0 + buf[0] * PRIME_2, 13) * PRIME_1;
      s1 = RotateLeft(s1 + buf[1] * PRIME_2, 13) * PRIME_1;
      s2 = RotateLeft(s2 + buf[2] * PRIME_2, 13) * PRIME_1;
      s0 = RotateLeft(s0 + buf[3] * PRIME_2, 13) * PRIME_1;
      s1 = RotateLeft(s1 + buf[4] * PRIME_2, 13) * PRIME_1;
      s2 = RotateLeft(s2 + buf[5] * PRIME_2, 13) * PRIME_1;
      s0 = RotateLeft(s0 + buf[6] * PRIME_2, 13) * PRIME_1;
      s1 = RotateLeft(s1 + buf[7] * PRIME_2, 13) * PRIME_1;
      data += 32;
    }
  }
  H = sz + RotateLeft(s0, 1) + RotateLeft(s1, 7) + RotateLeft(s2, 12);

#if __clang__
  __builtin_assume(data == stop_block);
#else
  __attribute__((__assume__(data == stop_block)));
#endif

  data = __builtin_assume_aligned(data, 32);
  while (data < stop_word) {
    H = RotateLeft(H + (*(const uint32_t *)data) * PRIME_3, 17) * PRIME_4;
    data += 4;
  }
  while (data < stop)
    H = RotateLeft(H + (*data++) * PRIME_5, 11) * PRIME_1;

  data = p;
  while (data < start_word)
    H = RotateLeft(H + (*data++) * PRIME_5, 11) * PRIME_1;
  while (data < start_block) {
    H = RotateLeft(H + (*(const uint32_t *)data) * PRIME_3, 17) * PRIME_4;
    data += 4;
  }

  H ^= H >> 15;
  H *= PRIME_2;
  H ^= H >> 13;
  H *= PRIME_3;
  H ^= H >> 16;

  return H;
}
