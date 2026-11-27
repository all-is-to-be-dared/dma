#ifndef __RISCV_DMA_H__
#define __RISCV_DMA_H__

#include <stdint.h>

struct rv_state {
  volatile _Alignas(256) uint32_t REGS[33]; // 31 extra
  volatile uint32_t PC;
  volatile uint32_t AMO_RESERVED;

  volatile uint32_t MSTATUS;
  volatile union {
    struct { uint32_t CYCLEL, CYCLEH; };
    uint64_t CYCLE64;
  };
  volatile union {
    struct { uint32_t TIMERL, TIMERH; };
    uint64_t TIMER64;
  };
  volatile uint32_t TIMERMATCHL, TIMERMATCH;

  volatile uint32_t MSCRATCH;
  volatile uint32_t MTVEC;
  volatile uint32_t MIE;
  volatile uint32_t MIP;

  volatile uint32_t MEPC;
  volatile uint32_t MTVAL;
  volatile uint32_t MCAUSE;

  volatile uint32_t PRIV;
  volatile uint32_t WFI;

  volatile uint32_t PLIC_UART_PRIORITY;
  volatile uint32_t PLIC_PENDING;
  volatile uint32_t PLIC_ENABLE;
  volatile uint32_t PLIC_THRESHOLD;
  volatile uint32_t PLIC_CLAIM;

	  volatile uint32_t UART_IER;
	  volatile uint32_t UART_LCR;
			  volatile uint32_t UART_MCR;
		  volatile uint32_t UART_RBR;
			  volatile uint32_t UART_LSR;
			  volatile uint32_t UART_MSR;
			  volatile uint32_t UART_SCR;
			  volatile uint32_t UART_DLL;
		  volatile uint32_t UART_DLM;
		  volatile uint32_t UART_IIR_THRI_PENDING;
		  volatile uint32_t UART_IIR_THRI_DELAY;
		  volatile uint32_t DEBUG_LOAD_ADDR;
		  volatile uint32_t DEBUG_LOAD_KIND;
		  volatile uint32_t DEBUG_LOAD_OUT;
	  
	  volatile uint32_t EMU_STATE;
  volatile uint32_t DEBUG_CYCLE_LIMIT;
  volatile uint32_t LAST_PC;
  volatile uint32_t PREV_PC;
};
enum {
  EMU_STATE_RUNNING = 0,
  EMU_STATE_PASS = 1,
  EMU_STATE_FAIL = 2,
  EMU_STATE_DEBUG_TIMEOUT = 3,
  EMU_STATE_DEBUG_TRAP = 4,
};

struct opaque_emulator;

extern volatile struct rv_state RISCV[1];

struct emulator {
  struct opaque_emulator *opaque;
  volatile struct rv_state state;
};
struct riscv_emulate_result {
  /// Number of nanoseconds emulator ran for.
  uint64_t nanos;
};

void riscv_emulate(
  struct emulator *emu,
  struct riscv_emulate_result *result
);
void riscv_emulator_init(struct emulator *emu);

// =================================================================================================
// SECTION: META-TABLE UTILITIES
// =================================================================================================

#define STRX(x) #x
#define STR(x) STRX(x)
#define DUMP_TAB_AD(tbl) printf(#tbl ":%p BUS:%lx\n", tbl, bus(tbl)._0)
#define DUMP_SUPERTAB_AD(tbl, sub, off)                                                            \
  printf(#tbl ":%p BUS:%lx " sub " = +" STR(off) "\n", tbl, bus(tbl)._0)

#define XCONCAT(a, b) a##b
#define CONCAT(a, b) XCONCAT(a, b)
#define BLKCNT(f) CONCAT(f, _BLKCNT)

#define STAMP_START bus_t __STAMP_START = ctx_label(ctx)
// #define STAMP_END(f)
//   BLKCNT(f)=ctx_label(ctx)._0-__STAMP_START._0
#define STAMP_ASSERT(f) assert((ctx_label(ctx)._0 - __STAMP_START._0) == BLKCNT(f) * 32)

extern volatile _Alignas(1 << 24) uint8_t TAB_SUPER[1 << 24];
extern volatile _Alignas(1 << 24) uint8_t TAB_JAL[1 << 24];
enum {
  // taken: 0-3
  SUPER_ADD = 4,            // 4
  SUPER_OP_A_RD = 8,        // 1
  SUPER_OP_A_FUNCT3_X4 = 9, // 1
  SUPER_OP_B_RS1_X4 = 10,   // 1
  SUPER_OP_C_RS2_X4 = 11,   // 1
  SUPER_OP_C_FUNCT7 = 12,   // 1
  SUPER_OP_A_RD_X4 = 13,    // 1
  // open: 14-15
  // taken: 16-19
  SUPER_SUB = 20,            // 4
  SUPER_OP_IMM12_SEXTW = 24, // 4
  SUPER_EQ = 28,             // 2
  SUPER_GEU = 30,            // 2
  // taken: 32-35
  // open: 36-47
  SUPER_GES = 36, // 4
  SUPER_MIN = 40, // 1
  SUPER_MUL = 41,
  SUPER_MULCRY = 42,
  // open: 43-47
  SUPER_OP_C_CSR_SRC = 48, // 4
  SUPER_OP_C_CSR_DST = 52, // 4
  SUPER_OP_B_ZIMM = 56,    // 1
  SUPER_OP_C_CSR32 = 60,   // 4
  // open: 64-255

  // SUPER is taken xxxxy0..xxxxy4 for all xxxxx
};

#endif
