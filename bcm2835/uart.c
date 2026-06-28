#include <bcm2835/platform.h>
#include <bcm2835/arch.h>

static uint16_t
baud_to_prescaler(uint32_t baud_rate, uint32_t core_freq)
{
  return (uint16_t)((core_freq / (8u * baud_rate)) - 1u);
}

void
aux_uart_init(uint32_t baud_rate, uint32_t core_freq)
{
  dsb();
  aux->enables |= AUX_ENABLE_UART;
  dsb();

  aux_uart->cntl = 0;
  aux_uart->iir = 0;
  aux_uart_clear_fifos();
  aux_uart->lcr = AUX_UART_LCR_8BIT;
  aux_uart->mcr = 0;
  aux_uart->baud = (uint32_t)baud_to_prescaler(baud_rate, core_freq);
  aux_uart->ier = 0;
  aux_uart->cntl = AUX_UART_CNTL_TX_ENABLE | AUX_UART_CNTL_RX_ENABLE;

  dsb();
}

void
aux_uart_clear_fifos(void)
{
  dsb();
  aux_uart->iir = AUX_UART_IIR_CLEAR_BOTH;
}

void
aux_uart_set_baud_rate(uint32_t baud_rate, uint32_t core_freq)
{
  dsb();
  aux_uart_clear_fifos();
  aux_uart->cntl &= ~(AUX_UART_CNTL_TX_ENABLE | AUX_UART_CNTL_RX_ENABLE);
  aux_uart->baud = (uint32_t)baud_to_prescaler(baud_rate, core_freq);
  // aux_uart->baud = baud_to_prescaler(baud_rate, core_freq);
  (void)aux_uart->lsr; // don't remember why i did this
  aux_uart_clear_fifos();
  aux_uart->cntl |= AUX_UART_CNTL_TX_ENABLE | AUX_UART_CNTL_RX_ENABLE;
  dsb();
}

bool
aux_uart_tx_idle(void)
{
  bool r = (aux_uart->stat & AUX_UART_STAT_TX_DONE) != 0;
  dsb();
  return r;
}

void
aux_uart_flush_tx_fifo(void)
{
  while (!aux_uart_tx_idle())
    ;
}

bool
aux_uart_can_put(void)
{
  bool r = (aux_uart->stat & AUX_UART_STAT_TX_SPACE) != 0;
  dsb();
  return r;
}

void
aux_uart_put(uint8_t b)
{
  dsb();
  while (!aux_uart_can_put())
    ;
  aux_uart->io = (uint32_t)b;
  dsb();
}

void
_putchar(char character)
{
  aux_uart_put(character);
}

bool
aux_uart_can_read(void)
{
  bool r = (aux_uart->stat & AUX_UART_STAT_RX_DATA) != 0;
  dsb();
  return r;
}

uint8_t
aux_uart_read(void)
{
  dsb();
  while (!aux_uart_can_read())
    ;
  uint8_t c = (uint8_t)(aux_uart->io);
  dsb();
  return c;
}
