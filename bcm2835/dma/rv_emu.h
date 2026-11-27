#pragma once

#include <stdint.h>

#include <bcm2835/dma/emit.h>

typedef struct {
  volatile alignas(256) uint32_t REGS[33];

  volatile uint32_t PC;

  volatile union {
    struct { uint32_t CYCLEL, CYCLEH; };
    uint64_t CYCLE64;
  };
  volatile uint32_t MTIMECMPLO, MTIMECMPHI;

  volatile uint32_t MSTATUS;
  volatile uint32_t MSTATUSH;

  volatile uint32_t MSCRATCH;
  volatile uint32_t MTVEC;
  volatile uint32_t MIE;
  volatile uint32_t MIP;

  volatile uint32_t MEPC;
  volatile uint32_t MTVAL;
  volatile uint32_t MCAUSE;

  volatile uint32_t PRIV;
  volatile uint32_t WFI;

  volatile uint32_t AMO_RESERVED;

  volatile uint32_t PLIC_PRIO[2];
  volatile uint32_t PLIC_PENDING[1];
  volatile uint32_t PLIC_ENABLE_0_31[2];
  volatile uint32_t PLIC_THRESHOLD[2];
  volatile uint32_t PLIC_CLAIM[2];

  volatile uint32_t UART_DR; // passthrough
  volatile uint32_t UART_RSR_ECR; // passthrough
  volatile uint32_t UART_FR; // passthrough
  volatile uint32_t UART_ILPR; // passthrough
  volatile uint32_t UART_IBRD; // passthrough
  volatile uint32_t UART_FPRD; // passthrough
  volatile uint32_t UART_LCR_H; // passthrough
  volatile uint32_t UART_CR; // passthrough
  // interrupt fifo level select
  // 000 = 1/8, 001 = 1/4, 010 = 1/2, 011 = 3/4, 100 = 7/8
  // interrupts are generated based on a transition through a level
  // 5:3 rxiflsel
  // 2:0 txiflsel
  volatile uint32_t UART_IFLS;
  // interrupt mask set/clear
  // 10 OEIM - UARTOEINTR - overrun error
  //  9 BEIM - UARTBEINTR - break error
  //  8 PEIM - UARTPEINTR - parity error
  //  7 FEIM - UARTFEINTR - frame error
  //  6 RTIM - UARTRTINTR - receive timeout interrupt
  //  5 TXIM - UARTTXINTR - transmit interrupt
  //  4 RXIM - UARTRXINTR - receive interrupt
  //  3 DSRMIM
  //  2 DCDMIM
  //  1 CTSMIM - UARTCTSINTR - nUARTCTS modem interrupt mask
  //  0 RIMIM
  volatile uint32_t UART_IMSC;
  // raw interrupt status
  // (as IMSC)
  volatile uint32_t UART_RIS;
  // masked interrupt status
  // (as IMSC)
  volatile uint32_t UART_MIS;
  // interrupt clear
  // (as IMSC)
  volatile uint32_t UART_ICR;
  // IGNORE
  volatile uint32_t UART_DMACR;
  
  volatile uint32_t UART_PERIPHID[4];
  volatile uint32_t UART_CELLID[4];

  volatile uint32_t EMU_STATE;
  volatile uint32_t EMU_FUEL;
} RVState;

enum {
  MARK_FST = 100,
  MARK_LUI = 200,
  MARK_AUIPC = 300,
  MARK_JAL = 400,
  MARK_JALR = 500,
  MARK_BCC = 600,
  MARK_LOAD = 700,
  MARK_STORE = 800,
  MARK_IMM = 900,
  MARK_REG = 1000,
  MARK_FENCE = 1100,
  MARK_ENV = 1200,
  MARK_ATOMIC = 1300,
  MARK_CUSTOM = 1400,
};

enum {
  EMU_STATE_RUNNING = 0,
  EMU_STATE_PASS = 1,
  EMU_STATE_FAIL = 2,
  EMU_STATE_DEBUG_TIMEOUT = 3,
  EMU_STATE_DEBUG_TRAP = 4,
};

enum {
  EMU_CUSTOM_0 = 0x0000'000b, // pass
  EMU_CUSTOM_1 = 0x0000'002b, // fail
  EMU_CUSTOM_2 = 0x0000'005b, // memcpy
  EMU_CUSTOM_3 = 0x0000'007b, // memset
};

extern volatile RVState RV[1];
extern volatile uint32_t mark;

enum {
  HYPERV_OP_A_RD = 0x84,
  HYPERV_OP_A_RD_X4 = 0x85,
  HYPERV_OP_A_FUNCT3_X4 = 0x86,
  HYPERV_OP_B_RS1_X4 = 0x87,
  HYPERV_OP_C_RS2_X4 = 0x88,
  HYPERV_OP_C_FUNCT7 = 0x89,
  HYPERV_OP_B_ZIMM = 0x8a,
  // available: 4b
  HYPERV_OP_IMM12_SEXTW = 0x8c, // 4

  HYPERV_OP_C_CSR_SRC = 0x94, // 4
  HYPERV_OP_C_CSR_DST = 0x98, // 4
  HYPERV_OP_C_CSR32 = 0x9c, // 4
  HYPERV_OP_C_ENV0 = 0xa4, // 1
};
enum {
  SUPERV_RegisterMapWrite = 0x80,
  SUPERV_RegisterMapRead = 0x81,
  SUPERV_OP5_MASK = 0x82,
  // Implements (x & 0x77) | (x & 0x8) << 4
  SUPERV_HandleMPIEOnTrap = 0x83,

  SUPERV_BccRead_12__10_5 = 0x84, // 4
  SUPERV_BccRead_11__4_1 = 0x88, // 2
  // Implements (x & 0xf8) >> 1
  SUPERV_Amo_Funct5x4 = 0x8a, // 1
  SUPERV_IsMmioAddrFromHiByteX4 = 0x8b, // 1
  SUPERV_HandleMPIEOnMRET = 0x8c, // 1
};

enum {
  MCAUSE_ILLEGAL = 2,
  MCAUSE_BREAKPOINT = 3,
  MCAUSE_LOAD_ACCESS = 5,
};

void
riscv_build_emulator(emit_ctx *ctx);

enum {
  EMU_SYSMEM_MMIO_HIGH = 0x0100'0000,
};
