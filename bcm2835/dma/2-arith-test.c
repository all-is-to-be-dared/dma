#include <inttypes.h>

#include <bcm2835/arch.h>
#include <bcm2835/platform.h>
#include <bcm2835/extra.h>
#include <bcm2835/ptags.h>

#include <bcm2835/dma/private.h>
#include <bcm2835/dma/emit.h>
#include <bcm2835/dma/arith.h>

#include <generic/math.h>
#include <generic/printf.h>

double
meanlf(const uint64_t *d, size_t n)
{
  uint64_t tot;
  size_t i;

  if (!n)
    return 0.;

  tot = 0;
  for (i = 0; i < n; i++)
    tot += d[i];
  return (double)tot / (double)n;
}
double
stdevlf(const uint64_t *d, size_t n)
{
  double mean;
  uint64_t acc;
  size_t i;

  if (!n)
    return 0;

  mean = meanlf(d, n);

  acc = 0;
  for (i = 0; i < n; i++)
    acc += d[i] * d[i];

  return __builtin_sqrt((double)acc / (double)n - mean * mean);
}

arena ARENA;

void
main(void)
{
  struct ptag_mem_region arm_mem, vc_mem;
  uint32_t i;
  uint16_t chanmask;

  gpio_pin_set_function(14, FSEL_ALT5);
  gpio_pin_set_function(15, FSEL_ALT5);

  aux_uart_init(1152000);

  systmr_delay_ms(1000);
  printf(__FILE__ ": starting\n");

  arm_mem = ptag_get_arm_memory();
  vc_mem = ptag_get_vc_memory();

  printf("Memory regions:\n");
  printf("\t\x1b[3mName\tBase    \tSize    \x1b[0m\n");
  printf("\tARM\t%08x\t%08x\n", arm_mem.base, arm_mem.size);
  printf("\tVC\t%08x\t%08x\n", vc_mem.base, vc_mem.size);
  printf("\n");

  chanmask = ptag_get_dma_channel_mask();
  printf("Channel mask: %08x\n", chanmask);
  printf("Available DMA channels:");
  for (i = 0; i < 16; i++)
    if (chanmask & (1 << i))
      printf(" \x1b[1m\x1b[32m[%d]\x1b[0m", i);
    else
      printf(" %d", i);
  printf("\n\n");

  HYPER = (void *)0x1000'0000;
  QHAT = HYPER + (1 << 24); // 0x1100'0000
  RHAT = QHAT + (1 << 24); // 0x1200'0000
  // enum {
  //   ARENA_SIZE = 1 << 24,
  // };
  // volatile uint8_t  *ARENA_BACKING = RHAT + (ARENA_SIZE);
  // ARENA = (arena){
  //   .start = ARENA_BACKING,
  //   .p = ARENA_BACKING,
  //   .end = ARENA_BACKING + (ARENA_SIZE),
  //   .util = 0,
  // };
  volatile uint8_t *ARENA_BACKING = RHAT + (1 << 24); // 0x1300'0000
  enum {
    ARENA_SIZE = 1 << 24,
  };
  ARENA = (arena){
    .start = (void*)ARENA_BACKING,
    .p = (void*)ARENA_BACKING,
    .end = (void*)ARENA_BACKING + (ARENA_SIZE),
    .util = 0,
  };

  printf("Initialized HYPER=%p SUPER=%p QHAT=%p RHAT=%p ARENA_BACKING=%p\n", HYPER, SUPER, QHAT, RHAT, ARENA_BACKING);

  dmakit_init_channels();
  struct dmakit_reservation rsv;
  struct dma_run_info runinfo;

  pmu_enable(); // enable cycle_count_read/write

  assert(dmakit_channel_acquire(&rsv, false, "ArithTest")); // Reserve a channel and dump RSVs
  dmakit_dump_channel_reservations();
  printf("\n");



  uint32_t init_start = cycle_count_read(), init_end;
  run_initializers(); // run all the magic INITIALIZERs
  init_end = cycle_count_read();
  printf("initializers ran in %dcy\n", init_end - init_start);

  enum
  {
    BLOCK_CNT = 2*32768
  };
  static cblk blocks[BLOCK_CNT];
  cblk *start;
  emit_ctx ctx = { .base = blocks, .p = blocks, .cap = BLOCK_CNT };
  // static uint8_t alignas(0x4000) ARENA[0x4000];
  // arena arena = {
  //   .start = ARENA,
  //   .p = ARENA,
  //   .end = ARENA + sizeof ARENA,
  //   .util = 0,
  // };

  enum
  {
    TIMING_COUNT = 256,
    REPETITION = 8,
  };

  uint64_t overhead = dma_get_launch_overhead(&ctx, rsv);

  printf("Basal overhead (1xDST_IGN): %lldns\n", overhead);

#define test_unsigned_unary(_Name_, _Args_, _Op_)                                           \
  {                                                                                         \
    constexpr uint32_t VALUES[] = _Args_;                                                   \
    volatile uint32_t a, b;                                                                 \
    b = 0;                                                                                  \
    start = Here(&ctx);                                                                     \
    for (int i = 0; i < REPETITION; i++)                                                    \
      _Name_(&ctx, 4, bus(&b), bus(&a));                                                    \
    End(&ctx);                                                                              \
    printf("\nChecking 4-wide " #_Name_ "\x1b[0m");                                         \
    for (int i = 0; i < _Countof(VALUES); i++) {                                            \
      a = VALUES[i];                                                                        \
      b = 0;                                                                                \
      assert(b == 0);                                                                       \
      runinfo = dmakit_timed_run(rsv, start);                                               \
      printf(".");                                                                          \
      assert(a == VALUES[i]);                                                               \
      assert(b == (_Op_), "post: b = %08" PRIx32 " (expected %08" PRIx32 ")\n", b, (_Op_)); \
    }                                                                                       \
    printf(" \x1b[32mOK\x1b[0m\n");                                                         \
    printf("\tTiming 4-wide " #_Name_ " x%d\n", REPETITION);                                \
    uint64_t times[TIMING_COUNT] = {};                                                      \
    for (int i = 0; i < TIMING_COUNT; i++) {                                                \
      a = VALUES[0];                                                                        \
      b = 0;                                                                                \
      runinfo = dmakit_timed_run(rsv, start);                                               \
      times[i] = dma_run_info_get_time_ns(runinfo) - overhead;                              \
      printf("\r\t");                                                                       \
      for (int j = max(0, i - 8); j <= i; j++) {                                            \
        if (((double)times[j] - meanlf(times, i)) > 2 * stdevlf(times, i))                  \
          printf("\x1b[31m");                                                               \
        else if (((double)times[j] - meanlf(times, i)) > stdevlf(times, i))                 \
          printf("\x1b[33m");                                                               \
        printf("% 8lldns\x1b[0m", times[j] / REPETITION);                                   \
      }                                                                                     \
    }                                                                                       \
    printf("\n\n\t\x1b[1;35m" #_Name_ "\x1b[0m | ν=\x1b[4m%.3f\x1b[0;2mkHz\x1b[0m "         \
           "μ=%.3f\x1b[2mcy\x1b[0m σ=%.3f\x1b[2mcy\x1b[0m \n\n",                            \
           (double)REPETITION * 1'000'000. / meanlf(times, TIMING_COUNT),                   \
           meanlf(times, TIMING_COUNT) / (double)REPETITION,                                \
           stdevlf(times, TIMING_COUNT) / (double)REPETITION);                              \
  }

#define test_unsigned_binary(_Name_, _Args_, _Op_, ...)                                         \
  {                                                                                             \
    constexpr uint32_t VALUES[][2] = _Args_;                                                    \
    volatile uint32_t a, b, c;                                                                  \
    start = Here(&ctx);                                                                         \
    for (int i = 0; i < REPETITION; i++)                                                        \
      _Name_(&ctx, 4, bus(&c), bus(&a), bus(&b) __VA_OPT__(, ) __VA_ARGS__);                    \
    End(&ctx);                                                                                  \
    printf("\nChecking 4-wide " #_Name_ "\x1b[0m");                                             \
    for (int i = 0; i < _Countof(VALUES); i++) {                                                \
      a = VALUES[i][0];                                                                         \
      b = VALUES[i][1];                                                                         \
      c = 0;                                                                                    \
      assert(c == 0);                                                                           \
      runinfo = dmakit_timed_run(rsv, start);                                                   \
      printf(".");                                                                              \
      assert(a == VALUES[i][0]);                                                                \
      assert(b == VALUES[i][1]);                                                                \
      assert(c == (_Op_),                                                                       \
             "post: c = %08" PRIx32 " (expected %08" PRIx32 " for a=%08" PRIx32 " b=%08" PRIx32 \
             ")\n",                                                                             \
             c,                                                                                 \
             (_Op_),                                                                            \
             a,                                                                                 \
             b);                                                                                \
    }                                                                                           \
    printf(" \x1b[32mOK\x1b[0m\n");                                                             \
    printf("\tTiming 4-wide " #_Name_ " x%d\n", REPETITION);                                    \
    uint64_t times[TIMING_COUNT] = {};                                                          \
    for (int i = 0; i < TIMING_COUNT; i++) {                                                    \
      a = VALUES[i][0];                                                                         \
      b = VALUES[i][1];                                                                         \
      c = 0;                                                                                    \
      runinfo = dmakit_timed_run(rsv, start);                                                   \
      times[i] = dma_run_info_get_time_ns(runinfo) - overhead;                                  \
      printf("\r\t");                                                                           \
      for (int j = max(0, i - 8); j <= i; j++) {                                                \
        if (((double)times[j] - meanlf(times, i)) > 2 * stdevlf(times, i))                      \
          printf("\x1b[31m");                                                                   \
        else if (((double)times[j] - meanlf(times, i)) > stdevlf(times, i))                     \
          printf("\x1b[33m");                                                                   \
        printf("% 8lldns\x1b[0m", times[j] / REPETITION);                                       \
      }                                                                                         \
    }                                                                                           \
    printf("\n\n\t\x1b[1;34m" #_Name_ "\x1b[0m | ν=\x1b[4m%.3f\x1b[0;2mkHz\x1b[0m "             \
           "μ=%.3f\x1b[2mcy\x1b[0m σ=%.3f\x1b[2mcy\x1b[0m \n\n",                                \
           (double)REPETITION * 1'000'000. / meanlf(times, TIMING_COUNT),                       \
           meanlf(times, TIMING_COUNT) / (double)REPETITION,                                    \
           stdevlf(times, TIMING_COUNT) / (double)REPETITION);                                  \
  }

  test_unsigned_unary(
    Inc,
    ((const uint32_t[]){
      0x00000000, 0x000000ff, 0x000001ff, 0x0000ffff, 0x0000ff00, 0x00ffffff, 0xffffffff }),
    a + 1);
  test_unsigned_binary(Add, ((const uint32_t[][2]){ { 0x12345678U, 0xfedcba98U } }), a + b);
  test_unsigned_binary(
    Sub, ((const uint32_t[][2]){ { 0x12345678U, 0x11 }, { 0x11, 0x12345678 } }), a - b);




  test_unsigned_unary(Not, ((const uint32_t[]){ 0x12345678UL }), ~a);
  test_unsigned_binary(And, ((const uint32_t[][2]){ { 0x12345678U, 0xfedcba98U } }), a & b);
  test_unsigned_binary(Or, ((const uint32_t[][2]){ { 0x12345678U, 0xfedcba98U } }), a | b);
  test_unsigned_binary(Xor, ((const uint32_t[][2]){ { 0x12345678U, 0xfedcba98U } }), a ^ b);




  test_unsigned_binary(Sll,
                       ((const uint32_t[][2]){ { 0x12345678, 0 },
                                               { 0x12345678, 1 },
                                               { 0x12345678, 7 },
                                               { 0x12345678, 8 },
                                               { 0x12345678, 9 },
                                               { 0x12345678, 15 },
                                               { 0x12345678, 16 },
                                               { 0x12345678, 21 },
                                               { 0x12345678, 31 },
                                               { 0x12345678, 32 },
                                               { 0x12345678, 33 } }),
                       a << (b % 32));
  test_unsigned_binary(Srl,
                       ((const uint32_t[][2]){ { 0x87654321, 0 },
                                               { 0x87654321, 1 },
                                               { 0x87654321, 7 },
                                               { 0x87654321, 8 },
                                               { 0x87654321, 9 },
                                               { 0x87654321, 15 },
                                               { 0x87654321, 16 },
                                               { 0x87654321, 21 },
                                               { 0x87654321, 31 },
                                               { 0x87654321, 32 },
                                               { 0x87654321, 33 } }),
                       a >> (b % 32));
  test_unsigned_binary(
    Sra,
    ((const uint32_t[][2]){
      { 0x12345678, 0 },  { 0x12345678, 1 },  { 0x12345678, 7 },  { 0x12345678, 8 },
      { 0x12345678, 9 },  { 0x12345678, 15 }, { 0x12345678, 16 }, { 0x12345678, 21 },
      { 0x12345678, 31 }, { 0x12345678, 32 }, { 0x12345678, 33 }, { 0x87654321, 0 },
      { 0x87654321, 1 },  { 0x87654321, 7 },  { 0x87654321, 8 },  { 0x87654321, 9 },
      { 0x87654321, 15 }, { 0x87654321, 16 }, { 0x87654321, 21 }, { 0x87654321, 31 },
      { 0x87654321, 32 }, { 0x87654321, 33 },
    }),
    (int32_t)a >> (b % 32));

  test_unsigned_binary(Geu,
                       ((const uint32_t[][2]){ { 0, 0 },
                                               { 1, 0 },
                                               { 0, 1 },
                                               { 0x100, 0 },
                                               { 0x200, 0x100 },
                                               { 0x2000, 0x1000 },
                                               { 0x20000, 0x10000 },
                                               { 0x10000, 0x20000 },
                                               { 0x1000000, 0x2000000 },
                                               { 0x2000000, 0x1000000 },
                                               { 0x1000000, 0x1000000 } }),
                       a >= b,
                       0);

  test_unsigned_binary(Ltu,
                       ((const uint32_t[][2]){ { 0, 0 },
                                               { 1, 0 },
                                               { 0, 1 },
                                               { 0x100, 0 },
                                               { 0x200, 0x100 },
                                               { 0x2000, 0x1000 },
                                               { 0x20000, 0x10000 },
                                               { 0x10000, 0x20000 },
                                               { 0x1000000, 0x2000000 },
                                               { 0x2000000, 0x1000000 },
                                               { 0x1000000, 0x1000000 } }),
                       4 * (a < b),
                       ICF_X4);

  test_unsigned_binary(Equ,
                       ((const uint32_t[][2]){ { 0, 0 },
                                               { 1, 0 },
                                               { 0, 1 },
                                               { 0x100, 0 },
                                               { 0x200, 0x100 },
                                               { 0x2000, 0x1000 },
                                               { 0x20000, 0x10000 },
                                               { 0x10000, 0x20000 },
                                               { 0x1000000, 0x2000000 },
                                               { 0x2000000, 0x1000000 },
                                               { 0x1000000, 0x1000000 } }),
                       a == b,
                       0);

  test_unsigned_binary(Neq,
                       ((const uint32_t[][2]){ { 0, 0 },
                                               { 1, 0 },
                                               { 0, 1 },
                                               { 0x100, 0 },
                                               { 0x200, 0x100 },
                                               { 0x2000, 0x1000 },
                                               { 0x20000, 0x10000 },
                                               { 0x10000, 0x20000 },
                                               { 0x1000000, 0x2000000 },
                                               { 0x2000000, 0x1000000 },
                                               { 0x1000000, 0x1000000 } }),
                       4 * (a != b),
                       ICF_X4);

  test_unsigned_binary(
    Ges,
    ((const uint32_t[][2]){ { 0, 0 }, { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 }, { -1, -1 } }),
    (int32_t)a >= (int32_t)b,
    0);
  test_unsigned_binary(
    Lts,
    ((const uint32_t[][2]){ { 0, 0 }, { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 }, { -1, -1 } }),
    (int32_t)a < (int32_t)b,
    0);

  constexpr uint32_t GOLDEN = 0x9E3779B9, GOLDEN2 = 0x7F4A7C15;

  extern void Div_32_32(emit_ctx * ctx, busad quot24, busad rem16, busad num32, busad den16);

  {
    constexpr uint32_t VALUES[][2] = ((const uint32_t[][2]){ { GOLDEN, GOLDEN2 }, {14, 0x10002}, {GOLDEN, GOLDEN}, {GOLDEN, 0x1ffff}, {0x1175600,0x12345} });
    volatile uint32_t a, b, c, d;
    start = Here(&ctx);
    for (int i = 0; i < REPETITION; i++)
      Div_32_32(&ctx, bus(&c), bus(&d), bus(&a), bus(&b));
    End(&ctx);
    printf("\nChecking 4-wide "
           "Div_32_32"
           "\x1b[0m");
    for (int i = 0; i < _Countof(VALUES); i++) {
      a = VALUES[i][0];
      b = VALUES[i][1];
      c = 0;
      d = 0;
      assert(c == 0);
      assert(d == 0);
      runinfo = dmakit_timed_run(rsv, start);
      printf(".");
      // ReportProbeInfo();
      assert(a == VALUES[i][0]);
      assert(b == VALUES[i][1]);
      assert(c == (a / b), "post: c = %08x (expected %08x for a=%08x b=%08x)\n", c, a/b, a, b);
      assert(d == (a % b), "post: d = %08x (expected %08x for a=%08x b=%08x)\n", d, a%b, a, b);
    }
    printf(" \x1b[32mOK\x1b[0m\n");
    printf("\tTiming 4-wide "
           "Div_32_32"
           " x%d\n",
           REPETITION);
    uint64_t times[TIMING_COUNT] = {};
    for (int i = 0; i < TIMING_COUNT; i++) {
      a = VALUES[0][0];
      b = VALUES[0][1];
      c = 0;
      runinfo = dmakit_timed_run(rsv, start);
      times[i] = dma_run_info_get_time_ns(runinfo) - overhead;
      printf("\r\t");
      for (int j = ({
             __typeof__(0) _a = (0);
             __typeof__(i - 8) _b = (i - 8);
             _a > _b ? _a : _b;
           });
           j <= i;
           j++) {
        if (((double)times[j] - meanlf(times, i)) > 2 * stdevlf(times, i))
          printf("\x1b[31m");
        else if (((double)times[j] - meanlf(times, i)) > stdevlf(times, i))
          printf("\x1b[33m");
        printf("% 8lldns\x1b[0m", times[j] / REPETITION);
      }
    }
    printf("\n\n\t\x1b[1;34m"
           "Div_32_32"
           "\x1b[0m | ν=\x1b[4m%.3f\x1b[0;2mkHz\x1b[0m "
           "μ=%.3f\x1b[2mcy\x1b[0m σ=%.3f\x1b[2mcy\x1b[0m \n\n",
           (double)REPETITION * 1'000'000. / meanlf(times, TIMING_COUNT),
           meanlf(times, TIMING_COUNT) / (double)REPETITION,
           stdevlf(times, TIMING_COUNT) / (double)REPETITION);
  };

  extern void Div_32_16(emit_ctx * ctx, busad quot24, busad rem16, busad num32, busad den16);

  {
    constexpr uint32_t VALUES[][2] = ((const uint32_t[][2]){ { GOLDEN2, 0x101 }, {14, 0x102}, {GOLDEN, 0x1234}, {GOLDEN, 0xffff}, {0x11756,0x1000} });
    volatile uint32_t a, b, c, d;
    start = Here(&ctx);
    for (int i = 0; i < REPETITION; i++)
      Div_32_16(&ctx, bus(&c), bus(&d), bus(&a), bus(&b));
    End(&ctx);
    printf("\nChecking 4-wide "
           "Div_32_16"
           "\x1b[0m");
    for (int i = 0; i < _Countof(VALUES); i++) {
      a = VALUES[i][0];
      b = VALUES[i][1];
      c = 0;
      d = 0;
      assert(c == 0);
      assert(d == 0);
      runinfo = dmakit_timed_run(rsv, start);
      printf(".");
      // ReportProbeInfo();
      assert(a == VALUES[i][0]);
      assert(b == VALUES[i][1]);
      assert(c == (a / b), "post: c = %08x (expected %08x for a=%08x b=%08x)\n", c, a/b, a, b);
      assert(d == (a % b), "post: d = %08x (expected %08x for a=%08x b=%08x)\n", d, a%b, a, b);
    }
    printf(" \x1b[32mOK\x1b[0m\n");
    printf("\tTiming 4-wide "
           "Div_32_16"
           " x%d\n",
           REPETITION);
    uint64_t times[TIMING_COUNT] = {};
    for (int i = 0; i < TIMING_COUNT; i++) {
      a = VALUES[0][0];
      b = VALUES[0][1];
      c = 0;
      runinfo = dmakit_timed_run(rsv, start);
      times[i] = dma_run_info_get_time_ns(runinfo) - overhead;
      printf("\r\t");
      for (int j = ({
             __typeof__(0) _a = (0);
             __typeof__(i - 8) _b = (i - 8);
             _a > _b ? _a : _b;
           });
           j <= i;
           j++) {
        if (((double)times[j] - meanlf(times, i)) > 2 * stdevlf(times, i))
          printf("\x1b[31m");
        else if (((double)times[j] - meanlf(times, i)) > stdevlf(times, i))
          printf("\x1b[33m");
        printf("% 8lldns\x1b[0m", times[j] / REPETITION);
      }
    }
    printf("\n\n\t\x1b[1;34m"
           "Div_32_16"
           "\x1b[0m | ν=\x1b[4m%.3f\x1b[0;2mkHz\x1b[0m "
           "μ=%.3f\x1b[2mcy\x1b[0m σ=%.3f\x1b[2mcy\x1b[0m \n\n",
           (double)REPETITION * 1'000'000. / meanlf(times, TIMING_COUNT),
           meanlf(times, TIMING_COUNT) / (double)REPETITION,
           stdevlf(times, TIMING_COUNT) / (double)REPETITION);
  };

  extern void Div_32_8(emit_ctx * ctx, busad quot32, busad rem8, busad num32, busad den8);

  {
    constexpr uint32_t VALUES[][2] = ((const uint32_t[][2]){ { GOLDEN2, 1 }, {14, 2}, {GOLDEN, 27}, {GOLDEN, 0xff}, {11756,15} });
    volatile uint32_t a, b, c, d;
    start = Here(&ctx);
    for (int i = 0; i < REPETITION; i++)
      Div_32_8(&ctx, bus(&c), bus(&d), bus(&a), bus(&b));
    End(&ctx);
    printf("\nChecking 4-wide "
           "Div_32_8"
           "\x1b[0m");
    for (int i = 0; i < _Countof(VALUES); i++) {
      a = VALUES[i][0];
      b = VALUES[i][1];
      c = 0;
      d = 0;
      assert(c == 0);
      assert(d == 0);
      runinfo = dmakit_timed_run(rsv, start);
      printf(".");
      // ReportProbeInfo();
      assert(a == VALUES[i][0]);
      assert(b == VALUES[i][1]);
      assert(c == (a / b), "post: c = %08x (expected %08x for a=%08x b=%08x)\n", c, a/b, a, b);
      assert(d == (a % b), "post: d = %08x (expected %08x for a=%08x b=%08x)\n", d, a%b, a, b);
    }
    printf(" \x1b[32mOK\x1b[0m\n");
    printf("\tTiming 4-wide "
           "Div_32_8"
           " x%d\n",
           REPETITION);
    uint64_t times[TIMING_COUNT] = {};
    for (int i = 0; i < TIMING_COUNT; i++) {
      a = VALUES[0][0];
      b = VALUES[0][1];
      c = 0;
      runinfo = dmakit_timed_run(rsv, start);
      times[i] = dma_run_info_get_time_ns(runinfo) - overhead;
      printf("\r\t");
      for (int j = ({
             __typeof__(0) _a = (0);
             __typeof__(i - 8) _b = (i - 8);
             _a > _b ? _a : _b;
           });
           j <= i;
           j++) {
        if (((double)times[j] - meanlf(times, i)) > 2 * stdevlf(times, i))
          printf("\x1b[31m");
        else if (((double)times[j] - meanlf(times, i)) > stdevlf(times, i))
          printf("\x1b[33m");
        printf("% 8lldns\x1b[0m", times[j] / REPETITION);
      }
    }
    printf("\n\n\t\x1b[1;34m"
           "Div_32_8"
           "\x1b[0m | ν=\x1b[4m%.3f\x1b[0;2mkHz\x1b[0m "
           "μ=%.3f\x1b[2mcy\x1b[0m σ=%.3f\x1b[2mcy\x1b[0m \n\n",
           (double)REPETITION * 1'000'000. / meanlf(times, TIMING_COUNT),
           meanlf(times, TIMING_COUNT) / (double)REPETITION,
           stdevlf(times, TIMING_COUNT) / (double)REPETITION);
  };

  {
    constexpr uint32_t VALUES[][2] = ((const uint32_t[][2]){
      { GOLDEN, GOLDEN2 }, {14, 0x10002}, {GOLDEN, GOLDEN}, {GOLDEN, 0x1ffff}, {0x1175600,0x12345},
      { GOLDEN2, 0x101 }, {14, 0x102}, {GOLDEN, 0x1234}, {GOLDEN, 0xffff}, {0x11756,0x1000},
      { GOLDEN2, 1 }, {14, 2}, {GOLDEN, 27}, {GOLDEN, 0xff}, {11756,15} });
    volatile uint32_t a, b, c, d;
    start = Here(&ctx);
    for (int i = 0; i < REPETITION; i++) {
      UDiv32(&ctx, bus(&c), bus(&d), bus(&a), bus(&b));
    }

    End(&ctx);
    printf("\nChecking 4-wide "
           "UDiv32"
           "\x1b[0m");
    for (int i = 0; i < _Countof(VALUES); i++) {
      a = VALUES[i][0];
      b = VALUES[i][1];
      c = 0;
      d = 0;
      assert(c == 0);
      assert(d == 0);
      runinfo = dmakit_timed_run(rsv, start);
      printf(".");
      // ReportProbeInfo();
      assert(a == VALUES[i][0]);
      assert(b == VALUES[i][1]);
      assert(c == (a / b), "post: c = %08x (expected %08x for a=%08x b=%08x)\n", c, a/b, a, b);
      assert(d == (a % b), "post: d = %08x (expected %08x for a=%08x b=%08x)\n", d, a%b, a, b);
    }
    printf(" \x1b[32mOK\x1b[0m\n");
    printf("\tTiming 4-wide "
           "UDiv32"
           " x%d\n",
           REPETITION);
    uint64_t times[TIMING_COUNT] = {};
    for (int i = 0; i < TIMING_COUNT; i++) {
      a = VALUES[0][0];
      b = VALUES[0][1];
      c = 0;
      runinfo = dmakit_timed_run(rsv, start);
      times[i] = dma_run_info_get_time_ns(runinfo) - overhead;
      printf("\r\t");
      for (int j = ({
             __typeof__(0) _a = (0);
             __typeof__(i - 8) _b = (i - 8);
             _a > _b ? _a : _b;
           });
           j <= i;
           j++) {
        if (((double)times[j] - meanlf(times, i)) > 2 * stdevlf(times, i))
          printf("\x1b[31m");
        else if (((double)times[j] - meanlf(times, i)) > stdevlf(times, i))
          printf("\x1b[33m");
        printf("% 8lldns\x1b[0m", times[j] / REPETITION);
      }
    }
    printf("\n\n\t\x1b[1;34m"
           "UDiv32"
           "\x1b[0m | ν=\x1b[4m%.3f\x1b[0;2mkHz\x1b[0m "
           "μ=%.3f\x1b[2mcy\x1b[0m σ=%.3f\x1b[2mcy\x1b[0m \n\n",
           (double)REPETITION * 1'000'000. / meanlf(times, TIMING_COUNT),
           meanlf(times, TIMING_COUNT) / (double)REPETITION,
           stdevlf(times, TIMING_COUNT) / (double)REPETITION);
  };

  {
    constexpr int32_t VALUES[][2] = ((const int32_t[][2]){
      { GOLDEN, GOLDEN2 }, {14, 0x10002}, {GOLDEN, GOLDEN}, {GOLDEN, 0x1ffff}, {0x1175600,0x12345},
      { GOLDEN2, 0x101 }, {14, 0x102}, {GOLDEN, 0x1234}, {GOLDEN, 0xffff}, {0x11756,0x1000},
      { GOLDEN2, 1 }, {14, 2}, {GOLDEN, 27}, {GOLDEN, 0xff}, {11756,15}, {1, -1}, {-1,1}, {-1,-1} });
    volatile int32_t a, b, c, d;
    start = Here(&ctx);
    for (int i = 0; i < REPETITION; i++) {
      SDiv32(&ctx, bus(&c), bus(&d), bus(&a), bus(&b));
    }

    End(&ctx);
    printf("\nChecking 4-wide "
           "SDiv32"
           "\x1b[0m");
    for (int i = 0; i < _Countof(VALUES); i++) {
      a = VALUES[i][0];
      b = VALUES[i][1];
      c = 0;
      d = 0;
      assert(c == 0);
      assert(d == 0);
      runinfo = dmakit_timed_run(rsv, start);
      printf(".");
      // ReportProbeInfo();
      assert(a == VALUES[i][0]);
      assert(b == VALUES[i][1]);
      assert(c == (a / b), "post: c = %08x (expected %08x for a=%08x b=%08x)\n", c, a/b, a, b);
      assert(d == (a % b), "post: d = %08x (expected %08x for a=%08x b=%08x)\n", d, a%b, a, b);
    }
    printf(" \x1b[32mOK\x1b[0m\n");
    printf("\tTiming 4-wide "
           "SDiv32"
           " x%d\n",
           REPETITION);
    uint64_t times[TIMING_COUNT] = {};
    for (int i = 0; i < TIMING_COUNT; i++) {
      a = VALUES[0][0];
      b = VALUES[0][1];
      c = 0;
      runinfo = dmakit_timed_run(rsv, start);
      times[i] = dma_run_info_get_time_ns(runinfo) - overhead;
      printf("\r\t");
      for (int j = ({
             __typeof__(0) _a = (0);
             __typeof__(i - 8) _b = (i - 8);
             _a > _b ? _a : _b;
           });
           j <= i;
           j++) {
        if (((double)times[j] - meanlf(times, i)) > 2 * stdevlf(times, i))
          printf("\x1b[31m");
        else if (((double)times[j] - meanlf(times, i)) > stdevlf(times, i))
          printf("\x1b[33m");
        printf("% 8lldns\x1b[0m", times[j] / REPETITION);
      }
    }
    printf("\n\n\t\x1b[1;34m"
           "SDiv32"
           "\x1b[0m | ν=\x1b[4m%.3f\x1b[0;2mkHz\x1b[0m "
           "μ=%.3f\x1b[2mcy\x1b[0m σ=%.3f\x1b[2mcy\x1b[0m \n\n",
           (double)REPETITION * 1'000'000. / meanlf(times, TIMING_COUNT),
           meanlf(times, TIMING_COUNT) / (double)REPETITION,
           stdevlf(times, TIMING_COUNT) / (double)REPETITION);
  };

  printf("Finished.\n");

  aux_uart_flush_tx_fifo();
}
