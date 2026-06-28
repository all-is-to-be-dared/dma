#include <bcm2835/extra.h>
#include <printf/printf.h>
#include <bcm2835/platform.h>

void
__assert(bool cond, const char* condstr, const char* file, long line)
{
  if (!cond) {
    printf("\x1b[31mPANIC\x1b[0m: assertion '%s' failed\n", condstr);
    printf("       in %s:%ld\n", file, line);
    aux_uart_flush_tx_fifo();
    pwrman_reset();
  }
}

void
__assert_fmt(bool cond, const char* condstr, const char* file, long line, const char* fmt, ...)
{
  va_list ap;

  if (!cond) {
    va_start(ap, fmt);
    printf("\x1b[31mPANIC\x1b[0m: assertion '%s' failed: ", condstr);
    vprintf(fmt, ap);
    printf("       in %s:%ld\n", file, line);
    va_end(ap);
    aux_uart_flush_tx_fifo();
    pwrman_reset();
  }
}

[[noreturn]]
void
__panic(const char* file, long line, const char* fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  printf("\x1b[31mPANIC\x1b[0m: ");
  vprintf(fmt, ap);
  printf("       in %s:%ld\n", file, line);
  va_end(ap);
  aux_uart_flush_tx_fifo();
  pwrman_reset();
}
