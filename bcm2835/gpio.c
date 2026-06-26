#include "bcm2835/platform.h"

void
gpio_pin_set_function(uint8_t pin, fsel_t fsel)
{
  // TODO: assert(pin <= 53);
  if (pin > 53)
    return;
  int reg_no = pin / 10;
  int shift = (pin % 10) * 3;
  auto r = gpio->gpfsel + reg_no;
  dsb();
  *r = ((*r) & ~(7 << shift)) | (fsel << shift);
  dsb();
}

void
gpio_pin_write(uint8_t pin, bool lev)
{
  if (pin > 53)
    return;

  auto r = (lev ? gpio->gpset : gpio->gpclr) + (pin / 32);

  dsb();
  *r = 1u << (pin % 32);

  return;
}
