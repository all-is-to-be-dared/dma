//! BCM2835 Power Management subsystem; includes the reset watchdog.

#include <bcm2835/platform.h>
#include <bcm2835/arch.h>

void
pwrman_reset(void)
{
  dsb();
  pwrman->wdog = PWRMAN_COOKIE | 0x0000f;
  pwrman->rstc = PWRMAN_COOKIE | PWRMAN_RESET_FULL;

  while (1)
    ;
}
