#include <bcm2835/extra.h>
#include <generic/printf.h>
#include <bcm2835/platform.h>
#include <generic/backtrace.h>

static void
print_backtrace(void) {
  backtrace_cursor curs;
  uintptr_t pc;
  const char *sym;

  assert(backtrace_enabled());
  curs = backtrace_cursor_new();
  assert(backtrace_cursor_next(&curs)); // skip print_backtrace
  printf("Backtrace:\n");
  do {
    backtrace_cursor_read(&curs, &pc, &sym);
    printf("\tat %#10x in \x1b[1m%s\x1b[0m\n", pc, sym ? sym : "(unknown)");
  } while(backtrace_cursor_next(&curs));
}

void
__assert(bool cond, const char* condstr, const char* file, long line)
{
  if (!cond) {
    printf("\x1b[31mPANIC\x1b[0m: assertion '%s' failed\n", condstr);
    printf("       in %s:%ld\n", file, line);
    if(backtrace_enabled())
        print_backtrace();
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
    if(backtrace_enabled())
      print_backtrace();
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

  if(backtrace_enabled())
    print_backtrace();
  
  aux_uart_flush_tx_fifo();
  pwrman_reset();
}
