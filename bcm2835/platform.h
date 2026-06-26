#pragma once

#include <stdint.h>
#include <stddef.h>

#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)
#define REG(off, ty, name)                                                                         \
  struct                                                                                           \
  {                                                                                                \
    uint32_t CONCAT(__pad, __COUNTER__)[off / sizeof(uint32_t)];                                   \
    ty name;                                                                                       \
  }
#define PAD(from, to) uint8_t CONCAT(__pad, __COUNTER__)[(to) - (from)]

volatile uint32_t typedef io32;

/*-------------------------------------------------------------------------------------------------
 * GENERAL
 */

enum
{
  SYSTMR_BASE = 0x2000'3000,
  PWRMAN_BASE = 0x2010'0000,
  GPIO_BASE = 0x2020'0000,
  AUX_BASE = 0x2021'5000,
  AUX_UART_BASE = 0x2021'5040,
};

/*-------------------------------------------------------------------------------------------------
 * SYSTEM TIMER
 */

typedef struct {
  io32 cs;
  io32 clo;
  io32 chi;
  io32 cmp[4];
} hw_systmr_t;
[[maybe_unused]]
static hw_systmr_t* systmr = (hw_systmr_t*)SYSTMR_BASE;

uint64_t systmr_read_raw(void);

/*-------------------------------------------------------------------------------------------------
 * POWER MANAGEMENT
 */

enum
{
  PWRMAN_COOKIE = 0x5a00'0000,
  PWRMAN_RESET_FULL = (1 << 5),
};
typedef union
{
  REG(0x1C, io32, rstc);
  REG(0x24, io32, wdog);
} hw_pwrman_t;
[[maybe_unused]]
static hw_pwrman_t* pwrman = (hw_pwrman_t*)PWRMAN_BASE;

[[noreturn]]
void
pwrman_reset(void);

/*-------------------------------------------------------------------------------------------------
 * GPIO
 */

typedef enum {
  FSEL_INPT = 0,
  FSEL_OUTP = 1,
  FSEL_ALT0 = 4,
  FSEL_ALT1 = 5,
  FSEL_ALT2 = 6,
  FSEL_ALT3 = 7,
  FSEL_ALT4 = 3,
  FSEL_ALT5 = 2,
} fsel_t;
typedef union
{
  REG(0x00, io32, gpfsel[6]);
  REG(0x1C, io32, gpset[2]);
  REG(0x28, io32, gpclr[2]);
  REG(0x34, io32, gplev[2]);
  REG(0x40, io32, gpeds[2]);
  REG(0x4C, io32, gpren[2]);
  REG(0x58, io32, gpfen[2]);
  REG(0x64, io32, gphen[2]);
  REG(0x70, io32, gplen[2]);
  REG(0x7C, io32, gparen[2]);
  REG(0x88, io32, gpafen[2]);
  REG(0x94, io32, gppud);
  REG(0x98, io32, gppudclk[2]);
} hw_gpio_t;
[[maybe_unused]]
static hw_gpio_t* gpio = (hw_gpio_t*)GPIO_BASE;

void
gpio_pin_set_function(
  uint8_t pin,
  fsel_t fsel
);

void gpio_pin_write(uint8_t pin, bool lev);

/*-------------------------------------------------------------------------------------------------
 * AUXILIARY CONTROLLER
 */

enum
{
  AUX_ENABLE_UART = (1 << 0),
};
typedef union
{
  REG(0x00, io32, irq);
  REG(0x04, io32, enables);
} hw_aux_t;
[[maybe_unused]]
static hw_aux_t* aux = (hw_aux_t*)AUX_BASE;

enum
{
  AUX_UART_IIR_CLEAR_BOTH = (3 << 1),
  AUX_UART_LCR_8BIT = (3 << 0),
  AUX_UART_CNTL_TX_ENABLE = (1 << 1),
  AUX_UART_CNTL_RX_ENABLE = (1 << 0),
  AUX_UART_STAT_TX_DONE = (1 << 9),
  AUX_UART_STAT_TX_SPACE = (1 << 1),
  AUX_UART_STAT_RX_DATA = (1 << 0),
};
typedef struct
{
  io32 io;
  io32 ier;
  io32 iir;
  io32 lcr;
  io32 mcr;
  io32 lsr;
  io32 msr;
  io32 scratch;
  io32 cntl;
  io32 stat;
  io32 baud;
} hw_aux_uart_t;
static_assert(offsetof(hw_aux_uart_t, baud) == (0x68 - 0x40), "hw_uax_uart_t has wrong layout");
[[maybe_unused]]
static hw_aux_uart_t* aux_uart = (hw_aux_uart_t*)AUX_UART_BASE;

void
aux_uart_init(uint32_t baud_rate, uint32_t core_freq);

void
aux_uart_clear_fifos();

void
aux_uart_set_baud_rate(uint32_t baud_rate, uint32_t core_freq);

bool
aux_uart_tx_idle(void);

void
aux_uart_flush_tx_fifo(void);

void
aux_uart_put(uint8_t b);

bool
aux_uart_can_read(void);

uint8_t
aux_uart_read(void);

/*-------------------------------------------------------------------------------------------------
 * MISCELLANEOUS
 */

static inline void
dsb(void)
{
  __asm__ volatile("mcr p15, 0, %0, c7, c10, 4" ::"r"(0u));
}

/*-------------------------------------------------------------------------------------------------
 * PROGRAM EXECUTION
 */

void
main(void);
