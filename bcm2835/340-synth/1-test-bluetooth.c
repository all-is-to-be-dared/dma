#include "bcm2835/arch.h"
#include "bcm2835/mmu.h"
#include <bcm2835/platform.h>
#include <bcm2835/ptags.h>

#include <generic/assert.h>
#include <generic/printf.h>


// Step 1. PL011 driver

enum
{
  PL011_BASE = 0x2020'1000,
};

typedef struct
{
  REG(0x00, io32, dr);
  REG(0x04, io32, rsrecr); // don't read until AFTER reading DR

  REG(0x18, io32, fr);
  REG(0x20, io32, ilpr); // not in use

  // XXX: do not change IBRD, FBRD, LCRH
  //        1. when UART is enabled
  //        2. when completing a transmission or a reception when it has been programmed to become
  //           disabled
  // bauddiv = FUARTCLK / (16 * BaudRate)
  REG(0x24, io32, ibrd);
  REG(0x28, io32, fbrd);
  REG(0x2c, io32, lcrh); // line control register

  REG(0x30, io32, cr); // control register

  REG(0x34, io32, ifls);  // interrupt fifo level select
  REG(0x38, io32, imsc);  // interrupt mask set/clear
  REG(0x3c, io32, ris);   // raw interrupt status
  REG(0x40, io32, mis);   // masked interrupt status
  REG(0x44, io32, icr);   // interrupt clear
  REG(0x48, io32, dmacr); // not available
  REG(0x80, io32, itcr);
  REG(0x84, io32, itip);
  REG(0x88, io32, itop);
  REG(0x8c, io32, tdr);
} hw_pl011_t;
static hw_pl011_t *pl011 = (hw_pl011_t *)PL011_BASE;

void
init_pl011_for_bluetooth(void)
{
  uint32_t uart_clock, quot, modu;

  uart_clock = ptag_get_nominal_clock_rate(PCID_UART);

  dsb();

  quot = uart_clock / (16 * 115200);
  modu = uart_clock % (16 * 115200);
  modu = (1 << 6) * modu / uart_clock;

  printf("PL011 BRD: %d.%d/32\n", quot, modu);

  assert(quot <= UINT16_MAX);
  assert(modu < (1 << 6));

  pl011->cr &= ~1; // disable

  while (pl011->fr & (1 << 3)) // while BUSY
    ;

  while(!(pl011->fr & 0x10)) // drain RX FIFO
    (void)pl011->dr;

  pl011->lcrh &= ~(1 << 4); // clear transmit FIFO

  pl011->ibrd = quot; // ibrd is 16 active bits
  pl011->fbrd = modu; // fbrd is 6 active bits
  // 8n1 no break
  pl011->lcrh = (3 << 5)   // 8
                | (1 << 4) // enable FIFOs;
    ;

  pl011->cr = (1 << 15)   // CTSEN
              | (1 << 14) // RTSEN
              | (1 << 9)  // RXE
              | (1 << 8)  // TXE
    ;

  pl011->cr |= 1; // enable

  dsb();
}

enum
{
  // Command & Event are only outbound, ACLData and SyncData are bidi
  HCIPT_Command = 1,
  HCIPT_ACLData = 2,
  HCIPT_SyncData = 3,
  HCIPT_Event = 4,
};

// On loss of synchronization from host to controller, controller shall send HCI_Hardware_Error, and
// will expect subsequent HCI_Reset.
// On loss of synchronization from controller to host, host shall send HCI_Reset, and re-synchronize
// by awaiting HCI_Command_Complete.

typedef struct {
  union {
    uint16_t opcode;
    struct {
      uint16_t opcode_command : 10;
      uint16_t opcode_group : 6;
    };
  };
  uint8_t param_tot_len;
  uint8_t params[];
} hci_command;

typedef struct {
  uint8_t code;
  uint8_t param_tot_len;
  uint8_t params[];
} hci_event;

void
main(struct elf_boot_args *boot_args)
{
  gpio_pin_set_function(14, FSEL_ALT5);
  gpio_pin_set_function(15, FSEL_ALT5);
  aux_uart_init(1152000);
  systmr_delay_ms(1000);
  printf(__FILE__ ": starting\n");

  static alignas(0x4000) struct mmu_translation_table ttb = {};
  mmu_init(&ttb);
  icache_set_enabled(true);
  brpdx_set_enabled(true);
  dcache_set_enabled(true);
  pmu_enable();

  // -----------------------------------------------------------------------------------------------
  
  gpio_pin_set_function(30, FSEL_ALT3); // CTS0
  gpio_pin_set_function(31, FSEL_ALT3); // RTS0
  gpio_pin_set_function(32, FSEL_ALT3); // TXD0
  gpio_pin_set_function(33, FSEL_ALT3); // RXD0

  init_pl011_for_bluetooth();

  

  // -----------------------------------------------------------------------------------------------
  
  printf("\nDONE.\n");
  aux_uart_flush_tx_fifo();
}
