#include <limits.h>
#include <inttypes.h>

#include <bcm2835/arch.h>
#include <bcm2835/platform.h>
#include <bcm2835/extra.h>
#include <bcm2835/ptags.h>
#include <generic/printf.h>
#include <generic/ubsan.h>
#include <generic/backtrace.h>
#include <generic/macros.h>




static uint32_t EXPECTED_REPORT_COUNT = 0;
#define UBSAN_TEST(s, e, ...)                                                   \
  {                                                                             \
    uint32_t initial_count = EXPECTED_REPORT_COUNT;                             \
    EXPECTED_REPORT_COUNT += (__VA_OPT__(1) - 1 ? 1 : __VA_ARGS__ - 0);         \
    e;                                                                          \
    if (ubsan_report_count() != EXPECTED_REPORT_COUNT)                          \
      printf("\n\x1b[31mTEST FAILED\x1b[0m: %s: expected %u reports, got %u\n", \
             s,                                                                 \
             EXPECTED_REPORT_COUNT - initial_count,                             \
             ubsan_report_count() - initial_count),                             \
        EXPECTED_REPORT_COUNT = ubsan_report_count();                           \
  }
void
main(struct elf_boot_args *boot_args)
{
  struct ptag_mem_region arm_mem, vc_mem;

  gpio_pin_set_function(14, FSEL_ALT5);
  gpio_pin_set_function(15, FSEL_ALT5);

  aux_uart_init(1152000, 250'000'000);

  systmr_delay_ms(1000);
  printf(__FILE__ ": starting\n");

  arm_mem = ptag_get_arm_memory();
  vc_mem = ptag_get_vc_memory();

  printf("Memory regions:\n");
  printf("\t\x1b[3mName\tBase    \tSize    \x1b[0m\n");
  printf("\tARM\t%08x\t%08x\n", arm_mem.base, arm_mem.size);
  printf("\tVC\t%08x\t%08x\n", vc_mem.base, vc_mem.size);
  printf("\n");

  printf("Boot args:\n");
  printf("\t\x1b[3mName    \tValue\x1b[0m\n");
  printf("\tELF base\t0x%p\n", boot_args->elf);
  printf("\tELF size\t%zu\n", boot_args->elf_size);
  printf("\tCmdline \t<%.*s>\n", boot_args->cmdline_len, boot_args->cmdline);
  printf("\n");

  if (!backtrace_enable(boot_args->elf))
    panic("Failed to install backtrace machinery!\n");

  UBSAN_TEST("signed-integer-overflow,binary", {

    signed _BitInt(33) x = 0x1'0000'0000wb;
    x -= 1;
    BLACK_BOX(x);
  })

  UBSAN_TEST("signed-integer-overflow,negation", {
    int x = INT_MIN;
    x = -x;
    BLACK_BOX(x);
  })

  UBSAN_TEST("signed-integer-overflow,division", {
    int x = INT_MIN;
    x /= -1;
    BLACK_BOX(x);
  })

  UBSAN_TEST("divide-by-zero,integer", {
    int x = 1234;
    _Pragma("GCC diagnostic push");
    _Pragma("GCC diagnostic ignored \"-Wdivision-by-zero\"");
    x /= 0;
    _Pragma("GCC diagnostic pop");
    BLACK_BOX(x);
  })

  UBSAN_TEST("divide-by-zero,float", {
    float x = 1.0;
    _Pragma("GCC diagnostic push");
    _Pragma("GCC diagnostic ignored \"-Wdivision-by-zero\"");
    x /= 0.0;
    _Pragma("GCC diagnostic pop");
    BLACK_BOX(x);
  })

  printf("DONE.\n");
  aux_uart_flush_tx_fifo();
}
