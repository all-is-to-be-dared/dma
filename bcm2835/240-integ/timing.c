#include <inttypes.h>
#include <string.h>

#include "bcm2835/arch.h"
#include "bcm2835/mmu.h"

#include <bcm2835/platform.h>
#include <bcm2835/ptags.h>

#include <generic/printf.h>
// #include <generic/integ.h>
#include <generic/assert.h>
#include <generic/backtrace.h>
#include <generic/macros.h>

uint32_t
xxh32(uint32_t seed, const void *p, size_t sz);

static _Alignas(4096*4) struct mmu_translation_table TTB = {};

void
main(struct elf_boot_args *boot_args)
{
  gpio_pin_set_function(14, FSEL_ALT5);
  gpio_pin_set_function(15, FSEL_ALT5);

  aux_uart_init(1152000, 400'000'000);

  systmr_delay_ms(1000);
  printf(__FILE__ ": starting\n");

  printf("Boot args:\n");
  printf("\t\x1b[3mName    \tValue\x1b[0m\n");
  printf("\tELF base\t0x%p\n", boot_args->elf);
  printf("\tELF size\t%zu\n", boot_args->elf_size);
  printf("\tCmdline \t<%.*s>\n", boot_args->cmdline_len, boot_args->cmdline);
  printf("\n");

  if (!backtrace_enable(boot_args->elf))
    printf("\n[ \x1b[33mWARNING\x1b[0m: Backtrace functionality not enabled! ]\n\n");

  mmu_init(&TTB);
  icache_set_enabled(true);
  brpdx_set_enabled(true);
  dcache_set_enabled(true);

  // -----------------------------------------------------------------------------------------------

  pmu_enable();

  uint32_t *p = (void*)0x1000'0000, *e = (void*)0x1800'0000;
  const size_t ep = (e-p)*sizeof *p;
  uint32_t *P, C;
  uint64_t us0, us1, dus;
  uint32_t c, hash;

  printf("===== [ TEST : SPEED OF LDMIA/8 ] =====\n");

  for (int i = 0; i < 4; i++) {
    us0 = systmr_read_raw();
    cycle_count_write(0);
    P = p;
    C = ep;
    __asm__ volatile (
      ".align 5\n"
      "2:\n"
      "pld [%[p],#64]\n"
      "ldm %[p]!, {r0-r7}\n"
      ".rept 21\n"
      "nop\n"
      ".endr\n"
      // "cmp %[p], %[e]\n"
      "subs %[c], %[c], #32\n"
      "bne 2b\n"
      : [p]"+r"(P),
        [c]"+r"(C)
      : // [e]"r"(e)
      : "memory", "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7"
      );
    c = cycle_count_read();
    us1 = systmr_read_raw();
    dus = us1 - us0;
    // rate = bytes / seconds = bytes / (micros / 1M) = bytes * 1M / micros
    printf("%#x bytes in %dcy / %llu.%03llums : %f MB/s\n", ep, c, dus / 1000, dus % 1000,
      (double)ep / (double)dus / 1024 * 1000 / 1024 * 1000
    );
  }

  printf("===== [ TEST : SPEED OF XXH32-MOD ] =====\n");

  for (int i = 0; i < 4; i++) {
    us0 = systmr_read_raw();
    cycle_count_write(0);
    hash = xxh32(0, p, ep);
    c = cycle_count_read();
    us1 = systmr_read_raw();
    dus = us1 - us0;
    printf("hash %#x bytes (%#10x) in %dcy / %llu.%03llums : %f MB/s\n", ep, hash, c, dus / 1000, dus % 1000,
      (double)ep / (double)dus / 1024 * 1000 / 1024 * 1000
    );
  }

  // -----------------------------------------------------------------------------------------------

  printf("DONE.\n");
  aux_uart_flush_tx_fifo();
}

typedef struct
{
  void *p;
  size_t s;
  uint32_t c;
} IntegRegion;

typedef struct
{
  uint32_t fcs32;
  IntegRegion regions[];
} IntegHeader;


// -------------------------------------------------------------------------------------------------
// XXHASH32. Based on Yann Collet's https://github.com/cyan4973/xxhash.

enum
{
  XXH_PRIME_1 = 2654435761U,
  XXH_PRIME_2 = 2246822519U,
  XXH_PRIME_3 = 3266489917U,
  XXH_PRIME_4 = 668265263U,
  XXH_PRIME_5 = 374761393U,

  XXH_MAX_BUFSZ = 16,
};
#define RotateLeft(x, r)                    \
  ({                                        \
    static_assert(__builtin_constant_p(r)); \
    __typeof__(x) t = x;                    \
    (t << r) | (t >> (-(r & 31) & 31));     \
  })

uint32_t
xxh32(uint32_t seed, const void *p, size_t sz)
{
  uint32_t bufsz, i, result, s0, s1, s2, s3;
  const uint8_t *data, *stop, *stop_block;
  _Alignas(4) uint8_t buf[XXH_MAX_BUFSZ];

  s0 = seed + XXH_PRIME_1 + XXH_PRIME_2;
  s1 = seed + XXH_PRIME_2;
  s2 = seed;
  s3 = seed - XXH_PRIME_1;

  bufsz = 0;
  result = sz;

  data = p;

  if (sz < XXH_MAX_BUFSZ) {
    while (sz--)
      buf[bufsz++] = *data++;

    goto hash;
  }

  stop = data + sz;
  stop_block = stop - XXH_MAX_BUFSZ;

  // TIME: 2192 for 0x800'0000
  /* Generated assembly:
  440:	e5914008 	ldr	r4, [r1, #8]
  444:	e591800c 	ldr	r8, [r1, #12]
  448:	e8910088 	ldm	r1, {r3, r7}
  44c:	e0200e94 	mla	r0, r4, lr, r0
  450:	e0266e98 	mla	r6, r8, lr, r6
  454:	e024be97 	mla	r4, r7, lr, fp
  458:	e0235e93 	mla	r3, r3, lr, r5
  45c:	e1a069e6 	ror	r6, r6, #19
  460:	e1a009e0 	ror	r0, r0, #19
  464:	e1a049e4 	ror	r4, r4, #19
  468:	e1a039e3 	ror	r3, r3, #19
  46c:	e0060996 	mul	r6, r6, r9
  470:	e0000990 	mul	r0, r0, r9
  474:	e00b0994 	mul	fp, r4, r9
  478:	e0050993 	mul	r5, r3, r9
  47c:	e2811010 	add	r1, r1, #16
  480:	e151000a 	cmp	r1, sl
  484:	9affffed 	bls	440 <xxh32+0x70>
   */
  // uint32_t *block;
  // while(data <= stop_block) {
  //   block = __builtin_assume_aligned(data, 4);
  //   s0 = RotateLeft(s0 + block[0] * XXH_PRIME_2, 13) * XXH_PRIME_1;
  //   s1 = RotateLeft(s1 + block[1] * XXH_PRIME_2, 13) * XXH_PRIME_1;
  //   s2 = RotateLeft(s2 + block[2] * XXH_PRIME_2, 13) * XXH_PRIME_1;
  //   s3 = RotateLeft(s3 + block[3] * XXH_PRIME_2, 13) * XXH_PRIME_1;
  //   data += 16;
  // }

  // // TIME: 1168ms for 0x800'0000
  // {
  //   // sz >= XXH_MAX_BUFSZ
  //   // then stop_block > data
  //   __asm__ (
  //     "2:"
  //     "ldmia %[dat]!, {r4-r7};"
  //     // mla rd, rm, rs, rn : rd := rm x rs + rn
  //     "mla %[s0], r4, %[prime2], %[s0];"
  //     "mla %[s1], r5, %[prime2], %[s1];"
  //     "mla %[s2], r6, %[prime2], %[s2];"
  //     "mla %[s3], r7, %[prime2], %[s3];"
  //     // ror rd, rm, rs
  //     "ror %[s0], %[s0], #19;"
  //     "ror %[s1], %[s1], #19;"
  //     "ror %[s2], %[s2], #19;"
  //     "ror %[s3], %[s3], #19;"
  //     // mul rd, rm, rs
  //     "mul %[s0], %[s0], %[prime1];"
  //     "mul %[s1], %[s1], %[prime1];"
  //     "mul %[s2], %[s2], %[prime1];"
  //     "mul %[s3], %[s3], %[prime1];"
  //     "cmp %[dat], %[stop];"
  //     "bls 2b;"
  //     : [dat]"+r"(data),
  //       [s0]"+r"(s0),
  //       [s1]"+r"(s1),
  //       [s2]"+r"(s2),
  //       [s3]"+r"(s3)
  //     : [prime1]"r"(XXH_PRIME_1),
  //       [prime2]"r"(XXH_PRIME_2),
  //       [stop]"r"(stop_block)
  //     : "r4", "r5", "r6", "r7"
  //   );
  // }

  // TIME: 1130ms (2.5%)
  // gain is probably due to squeezing out ~2cy/iter by putting the muls after the ldmia so that the
  // mlas don't have to wait (since branches don't speculate)
  //
  //         L1  M1 M2 M3 M4 A1 A2 A3 A4 CM R1 R2 R3 R4 B1
  //  cycles 1   2  2  2  2  2  2  2  2  1  1  1  1  1  x
  // latency 3,4 3  3  3  3  4  4  4  4  1  1  1  1  1  na
  //             R1 R2 R3 R4 L1 L1 L1 L1 L1 A1 A2 A3 A4 CM
  //                         M1 M2 M3 M4
  //
  // TODO: I just realized I don't think I have icache, dcache, or brpdx turned on right now.
  // XXX: Never mind, already had them icache and brpdx on.
  //
  // TODO: I think I might've calculated wrong, because I'm not accounting for pipeline fork when
  //       calculating the interaction between the A's and the R's; it's also not super clear to me
  //       what's going on there but it should be more addressable when I get rid of brpdx noise.
  // XXX: Actually, I think I didn't, it looks like I am actually getting bottlenecked by memory
  //      latency (?) here
  // {
  //   // sz >= XXH_MAX_BUFSZ
  //   // then stop_block > data
  //   __asm__ (
  //     "ldmia %[dat]!, {r4-r7};"
  //     "mla %[s0], r4, %[prime2], %[s0];"
  //     "mla %[s1], r5, %[prime2], %[s1];"
  //     "mla %[s2], r6, %[prime2], %[s2];"
  //     "mla %[s3], r7, %[prime2], %[s3];"

  //     "ror %[s0], %[s0], #19;"
  //     "ror %[s1], %[s1], #19;"
  //     "ror %[s2], %[s2], #19;"
  //     "ror %[s3], %[s3], #19;"

  //     "2:"
  //     // 1 clock, 2 memcy, result latency 3,3,4,4
  //     "ldmia %[dat]!, {r4-r7};"

  //     "mul %[s0], %[s0], %[prime1];"
  //     "mul %[s1], %[s1], %[prime1];"
  //     "mul %[s2], %[s2], %[prime1];"
  //     "mul %[s3], %[s3], %[prime1];"

  //     "mla %[s0], r4, %[prime2], %[s0];"
  //     "mla %[s1], r5, %[prime2], %[s1];"
  //     "mla %[s2], r6, %[prime2], %[s2];"
  //     "mla %[s3], r7, %[prime2], %[s3];"

  //     "ror %[s0], %[s0], #19;"
  //     "ror %[s1], %[s1], #19;"
  //     "ror %[s2], %[s2], #19;"
  //     "ror %[s3], %[s3], #19;"

  //     "cmp %[dat], %[stop];"
  //     "bls 2b;"

  //     "mul %[s0], %[s0], %[prime1];"
  //     "mul %[s1], %[s1], %[prime1];"
  //     "mul %[s2], %[s2], %[prime1];"
  //     "mul %[s3], %[s3], %[prime1];"

  //     : [dat]"+r"(data),
  //       [s0]"+r"(s0),
  //       [s1]"+r"(s1),
  //       [s2]"+r"(s2),
  //       [s3]"+r"(s3)
  //     : [prime1]"r"(XXH_PRIME_1),
  //       [prime2]"r"(XXH_PRIME_2),
  //       [stop]"r"(stop_block)
  //     : "r4", "r5", "r6", "r7"
  //   );
  // }

  // // TIME: ? ~900s ms ?
  // {
  //   register uint32_t _s0 asm("r0") = s1, _s1 asm("r1") = s1, _s2 asm("r2") = s2;
  //   register const uint8_t *_data asm("r3") = data;
  //   register uint32_t prime1 asm("r4") = XXH_PRIME_1, prime2 asm("r5") = XXH_PRIME_2;
  //   register const uint8_t *stop asm("r14") = stop_block;
  //   __asm__ (
  //     "push {r4-r12, r14};\n"
  //     "adr r7, 3f;\n"
  //     "str sp, [r7];\n"
  //     "mov r12, r4;\n"
  //     "mov r13, r5;\n"

  //     "2:\n"
  //     // 1 clock, 2 memcy, result latency 3,3,4,4
  //     "ldmia %[dat]!, {r4-r11};\n"

  //     "mul %[s0], %[s0], r12;"
  //     "mul %[s1], %[s1], r12;"
  //     "mul %[s2], %[s2], r12;"
  //     "mla %[s0], r4, r13, %[s0];"
  //     "mla %[s1], r5, r13, %[s1];"
  //     "mla %[s2], r6, r13, %[s2];"
  //     // 1 cycle stall
  //     "ror %[s0], %[s0], #19;"
  //     "ror %[s1], %[s1], #19;"
  //     "ror %[s2], %[s2], #19;"

  //     "mul %[s0], %[s0], r12;"
  //     "mul %[s1], %[s1], r12;"
  //     "mul %[s2], %[s2], r12;"
  //     "mla %[s0], r7, r13, %[s0];"
  //     "mla %[s1], r8, r13, %[s1];"
  //     "mla %[s2], r9, r13, %[s2];"
  //     "ror %[s0], %[s0], #19;"
  //     "ror %[s1], %[s1], #19;"
  //     "ror %[s2], %[s2], #19;"

  //     "mul %[s0], %[s0], r12;"
  //     "mul %[s1], %[s1], r12;"
  //     "mla %[s0], r10, r13, %[s0];"
  //     "mla %[s1], r11, r13, %[s1];"
  //     "ror %[s0], %[s0], #19;"
  //     "ror %[s1], %[s1], #19;"

  //     // 1 clock
  //     "cmp %[dat], r14;\n"
  //     // 0cy if folded dynamic prediction
  //     "bls 2b;\n"

  //     "mov r4, r12;\n"
  //     "mov r5, r13;\n"
  //     "adr r7, 3f;\n"
  //     "ldr sp, [r7];\n"
  //     "b 4f;\n"
  //     "3: .word 0\n"
  //     "4:\n"
  //     "pop {r4-r12, r14};\n"

  //     : [dat]"+r"(_data), // r3
  //       [s0]"+r"(_s0), // r0
  //       [s1]"+r"(_s1), // r1
  //       [s2]"+r"(_s2) // r2
  //     : "r"(prime1), // r4
  //       "r"(prime2), // r5
  //       "r"(stop) // r14
  //     : "memory"
  //   );

  //   data = _data;
  //   s0 = _s0;
  //   s1 = _s1;
  //   s2 = _s2;
  // }


  // -----------------------------------------------------------------------------------------------
  //                                xXx -*- DCACHE ON -*- xXx

  // TIME: 798ms
  // {
  //   // sz >= XXH_MAX_BUFSZ
  //   // then stop_block > data
  //   __asm__("ldmia %[dat]!, {r4-r7};"
  //           "mla %[s0], r4, %[prime2], %[s0];"
  //           "mla %[s1], r5, %[prime2], %[s1];"
  //           "mla %[s2], r6, %[prime2], %[s2];"
  //           "mla %[s3], r7, %[prime2], %[s3];"

  //           "ror %[s0], %[s0], #19;"
  //           "ror %[s1], %[s1], #19;"
  //           "ror %[s2], %[s2], #19;"
  //           "ror %[s3], %[s3], #19;"

  //           "2:"
  //           // 1 clock, 2 memcy, result latency 3,3,4,4

  //           "ldmia %[dat]!, {r4-r7};"

  //           "mul %[s0], %[s0], %[prime1];"
  //           "mul %[s1], %[s1], %[prime1];"
  //           "mul %[s2], %[s2], %[prime1];"
  //           "mul %[s3], %[s3], %[prime1];"

  //           "pld [%[dat], #16];"

  //           "mla %[s0], r4, %[prime2], %[s0];"
  //           "mla %[s1], r5, %[prime2], %[s1];"
  //           "mla %[s2], r6, %[prime2], %[s2];"
  //           "mla %[s3], r7, %[prime2], %[s3];"

  //           "ror %[s0], %[s0], #19;"
  //           "ror %[s1], %[s1], #19;"
  //           "ror %[s2], %[s2], #19;"
  //           "ror %[s3], %[s3], #19;"

  //           "cmp %[dat], %[stop];"
  //           "bls 2b;"

  //           "mul %[s0], %[s0], %[prime1];"
  //           "mul %[s1], %[s1], %[prime1];"
  //           "mul %[s2], %[s2], %[prime1];"
  //           "mul %[s3], %[s3], %[prime1];"

  //           : [dat] "+r"(data), [s0] "+r"(s0), [s1] "+r"(s1), [s2] "+r"(s2), [s3] "+r"(s3)
  //           : [prime1] "r"(XXH_PRIME_1), [prime2] "r"(XXH_PRIME_2), [stop] "r"(stop_block)
  //           : "r4", "r5", "r6", "r7");
  // }

  {
    register uint32_t _s0 asm("r0") = s1, _s1 asm("r1") = s1, _s2 asm("r2") = s2;
    register const uint8_t *_data asm("r3") = data;
    register uint32_t prime1 asm("r4") = XXH_PRIME_1, prime2 asm("r5") = XXH_PRIME_2;
    register const uint8_t *stop asm("r14") = stop_block;
    __asm__ (
      "push {r4-r12, r14}\n"
      "adr r7, 3f\n"
      "str sp, [r7]\n"
      "mov r12, r4\n"
      "mov r13, r5\n"

      "2:\n"
      // 1 clock, 2 memcy, result latency 3,3,4,4
      "pld [%[dat], #64]\n"
      "ldmia %[dat]!, {r4-r11};\n"

      "mul %[s0], %[s0], r12;"
      "mul %[s1], %[s1], r12;"
      "mul %[s2], %[s2], r12;"
      "mla %[s0], r4, r13, %[s0];"
      "mla %[s1], r5, r13, %[s1];"
      "mla %[s2], r6, r13, %[s2];"
      "ror %[s0], %[s0], #19;"
      "ror %[s1], %[s1], #19;"
      "ror %[s2], %[s2], #19;"

      "mul %[s0], %[s0], r12;"
      "mul %[s1], %[s1], r12;"
      "mul %[s2], %[s2], r12;"
      "mla %[s0], r7, r13, %[s0];"
      "mla %[s1], r8, r13, %[s1];"
      "mla %[s2], r9, r13, %[s2];"
      "ror %[s0], %[s0], #19;"
      "ror %[s1], %[s1], #19;"
      "ror %[s2], %[s2], #19;"

      "mul %[s0], %[s0], r12;"
      "mul %[s1], %[s1], r12;"
      "mla %[s0], r10, r13, %[s0];"
      "mla %[s1], r11, r13, %[s1];"
      "cmp %[dat], r14;\n" // cover a stall
      "ror %[s0], %[s0], #19;"
      "ror %[s1], %[s1], #19;"
      
      // 1 clock
      // 0cy if folded dynamic prediction
      "bls 2b;\n"

      "mov r4, r12;\n"
      "mov r5, r13;\n"
      "adr r7, 3f;\n"
      "ldr sp, [r7];\n"
      "b 4f;\n"
      "3: .word 0\n"
      "4:\n"
      "pop {r4-r12, r14};\n"

      : [dat]"+r"(_data), // r3
        [s0]"+r"(_s0), // r0
        [s1]"+r"(_s1), // r1
        [s2]"+r"(_s2) // r2
      : "r"(prime1), // r4
        "r"(prime2), // r5
        "r"(stop) // r14
      : "memory"
    );

    data = _data;
    s0 = _s0;
    s1 = _s1;
    s2 = _s2;
  }

  bufsz = stop - data;
  for (i = 0; i < bufsz; i++)
    buf[i] = data[i];

hash:
  if (result >= XXH_MAX_BUFSZ)
    result += RotateLeft(s0, 1) + RotateLeft(s1, 7) + RotateLeft(s2, 12) + RotateLeft(s3, 18);
  else
    result += s2 + XXH_PRIME_5;

  data = __builtin_assume_aligned(buf, 4);
  stop = data + bufsz;

  for (; data + 4 <= stop; data += 4)
    result = RotateLeft(result + *(uint32_t *)data * XXH_PRIME_3, 17) * XXH_PRIME_4;

  while (data != stop)
    result = RotateLeft(result + (*data++) * XXH_PRIME_5, 11) * XXH_PRIME_1;

  result ^= result >> 15;
  result *= XXH_PRIME_2;
  result ^= result >> 13;
  result *= XXH_PRIME_3;
  result ^= result >> 16;

  return result;
}
