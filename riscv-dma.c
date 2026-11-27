#include "rpi.h"
#include "printf.h"

#include "stamps.h"
#include "dma-impl.h"
#include "riscv-dma.h"
#include "ctx.h"

#ifndef RISCV_LOAD_DEBUG
#define RISCV_LOAD_DEBUG 0
#endif

static const inline _Bool bus_is_null(bus_t b) { return b._0 == 0; }
static const inline bus_t bus_null(void) { return (bus_t){0}; }

/// Universally available zero value
static volatile uint32_t K_ZERO = 0, K_ONE = 1;
volatile struct rv_state RISCV[1];

static volatile uint32_t CSR_SINK;
static volatile uint32_t CSR_MISA = 0x40001101; // RV32IMA + Zicsr/Zifencei-ish enough for probes.
static volatile uint32_t CSR_MHARTID = 0;

static volatile uint32_t MMIO_PLIC_UART_PRIORITY = 0x0c000028;
static volatile uint32_t MMIO_PLIC_PENDING = 0x0c001000;
static volatile uint32_t MMIO_PLIC_ENABLE = 0x0c002000;
static volatile uint32_t MMIO_PLIC_THRESHOLD = 0x0c200000;
static volatile uint32_t MMIO_PLIC_CLAIM = 0x0c200004;
static volatile uint32_t MMIO_CLINT_MTIMECMP_LO = 0x02004000;
static volatile uint32_t MMIO_CLINT_MTIMECMP_HI = 0x02004004;
static volatile uint32_t MMIO_CLINT_MTIME_LO = 0x0200bff8;
static volatile uint32_t MMIO_CLINT_MTIME_HI = 0x0200bffc;
static volatile uint32_t MMIO_UART_THR_RBR = 0x10000000;
static volatile uint32_t MMIO_UART_IER = 0x10000004;
static volatile uint32_t MMIO_UART_IIR_FCR = 0x10000008;
static volatile uint32_t MMIO_UART_LCR = 0x1000000c;
static volatile uint32_t MMIO_UART_MCR = 0x10000010;
static volatile uint32_t MMIO_UART_LSR = 0x10000014;
static volatile uint32_t MMIO_UART_MSR = 0x10000018;
static volatile uint32_t MMIO_UART_SCR = 0x1000001c;
static volatile uint32_t MMIO_PLIC_PRIORITY_BASE = 0x0c000000;
static volatile uint32_t MMIO_PLIC_PRIORITY_END = 0x0c00002c;

static volatile uint32_t *csr_read_ptr(uint32_t csr) {
  switch (csr) {
  case 0x300: return &RISCV->MSTATUS;
  case 0x301: return &CSR_MISA;
  case 0x304: return &RISCV->MIE;
  case 0x305: return &RISCV->MTVEC;
  case 0x340: return &RISCV->MSCRATCH;
  case 0x341: return &RISCV->MEPC;
  case 0x342: return &RISCV->MCAUSE;
  case 0x343: return &RISCV->MTVAL;
  case 0x344: return &RISCV->MIP;
  case 0xb00: return &RISCV->CYCLEL;
  case 0xb80: return &RISCV->CYCLEH;
  case 0xc00: return &RISCV->CYCLEL;
  case 0xc01: return &RISCV->TIMERL;
  case 0xc80: return &RISCV->CYCLEH;
  case 0xc81: return &RISCV->TIMERH;
  case 0xf14: return &CSR_MHARTID;
  default: return &K_ZERO;
  }
}

static volatile uint32_t *csr_write_ptr(uint32_t csr) {
  switch (csr) {
  case 0x300: return &RISCV->MSTATUS;
  case 0x304: return &RISCV->MIE;
  case 0x305: return &RISCV->MTVEC;
  case 0x340: return &RISCV->MSCRATCH;
  case 0x341: return &RISCV->MEPC;
  case 0x342: return &RISCV->MCAUSE;
  case 0x343: return &RISCV->MTVAL;
  case 0x344: return &RISCV->MIP;
  default: return &CSR_SINK;
  }
}

// =================================================================================================
// SECTION: RISC-V EMULATOR
// =================================================================================================

// Quick note: to emulate RISC-V's behavior of treating writes to x0 as
// discards, we have an extra register 'x32' which serves as a dump; writes to
// x0 map to x32 instead
static volatile _Alignas(256) uint8_t TAB_REGMAP_WR[32];
static volatile _Alignas(256) uint8_t TAB_REGMAP_RD[32];

void riscv_init_emulator_tables() {
  TAB_REGMAP_WR[0] = 128;
  TAB_REGMAP_RD[0] = 0;
  for (size_t i = 1; i < 32; i++) {
    TAB_REGMAP_WR[i] = 4 * i;
    TAB_REGMAP_RD[i] = 4 * i;
  }

  // <rd> and <funct3> are pulled from a 15:0 view of the opcode
  DUMP_SUPERTAB_AD(TAB_SUPER, "(op:<rd>x4)", SUPER_OP_A_RD_X4);
  DUMP_SUPERTAB_AD(TAB_SUPER, "(op:<funct3>x4)", SUPER_OP_FUNCT3_X4);
  for (size_t k = 0; k < 0x10000; k++) {
    // <rd> is 11:7
    TAB_SUPER[(k << 8) + SUPER_OP_A_RD] = (k >> 7) & 0x1f;
    // For custom R-type operands: rd is read as a source, so x0 maps to REGS[0].
    TAB_SUPER[(k << 8) + SUPER_OP_A_RD_X4] = (k >> 5) & 0x7c;
    // <funct3> is 14:12
    TAB_SUPER[(k << 8) + SUPER_OP_A_FUNCT3_X4] = (k >> 10) & 0x1c;
  }
  // <rs1> is pulled from a 23:8 view of the opcode
  DUMP_SUPERTAB_AD(TAB_SUPER, "(op:<rs1>x4)", SUPER_OP_RS1_X4);
  DUMP_SUPERTAB_AD(TAB_SUPER, "(op:<zimm>)", SUPER_OP_B_ZIMM);
  for (size_t k = 0; k < 0x10000; k++) {
    // <rs1> is 19:15; based against :8, that's 11:7
    TAB_SUPER[(k << 8) + SUPER_OP_B_RS1_X4] = (k >> 5) & 0x7c;
    TAB_SUPER[(k << 8) + SUPER_OP_B_ZIMM] = (k >> 7) & 0x1f;
  }
  // <rs2> and <funct7> are pulled from a 31:16 view of the opcode
  DUMP_SUPERTAB_AD(TAB_SUPER, "(op:<rs2>x4)", SUPER_OP_RS2_X4);
  DUMP_SUPERTAB_AD(TAB_SUPER, "(op:<funct7>x4)", SUPER_OP_FUNCT7);
  for (size_t k = 0; k < 0x10000; k++) {
    // <rs2> is 24:20; based against :16, that's 8:4
    TAB_SUPER[(k << 8) + SUPER_OP_C_RS2_X4] = (k >> 2) & 0x7c;
    // <funct7> is 31:25; based against :16, that's 15:9
    TAB_SUPER[(k << 8) + SUPER_OP_C_FUNCT7] = (k >> 9) & 0x7f;
  }
  DUMP_TAB_AD(TAB_JAL);
  for (size_t k = 0; k < 0x100000; k++) {
    // 20 | 10:1 | 11 | 19:12
    // 1     10    1     8
    // 19    9     8     0 in the instruction fragment; imm bit 20 is the sign bit.
    size_t p = (k & 0x000ff) << 12 | (k & 0x00100) << 3 | (k & 0x7fe00) >> 8 |
              ((k & 0x80000) ? 0xfff00000 : 0);
    __builtin_memcpy((void *)&TAB_JAL[(k << 4)], &p, 4);
  }
  for (size_t k = 0; k < 0x10000; k++) {
    uint16_t bits = k >> 4;
    if (k & 0x8000)
      bits |= 0xf000;
    TAB_SUPER[(k << 8) + SUPER_OP_IMM12_SEXTW + 0] = bits & 0xff;
    TAB_SUPER[(k << 8) + SUPER_OP_IMM12_SEXTW + 1] = bits >> 8;
    TAB_SUPER[(k << 8) + SUPER_OP_IMM12_SEXTW + 2] = (bits & 0x8000) ? 0xff : 0x00;
    TAB_SUPER[(k << 8) + SUPER_OP_IMM12_SEXTW + 3] = (bits & 0x8000) ? 0xff : 0x00;
  }
  DUMP_SUPERTAB_AD(TAB_SUPER, "(op:<csr-src>)", SUPER_OP_C_CSR_SRC);
  DUMP_SUPERTAB_AD(TAB_SUPER, "(op:<csr-dst>)", SUPER_OP_C_CSR_DST);
  DUMP_SUPERTAB_AD(TAB_SUPER, "(op:<csr>)", SUPER_OP_C_CSR32);
  for (size_t k = 0; k < 0x10000; k++) {
    uint32_t csr = k >> 4;
    uint32_t src = bus(csr_read_ptr(csr))._0;
    uint32_t dst = bus(csr_write_ptr(csr))._0;
    __builtin_memcpy((void *)&TAB_SUPER[(k << 8) + SUPER_OP_C_CSR_SRC], &src, 4);
    __builtin_memcpy((void *)&TAB_SUPER[(k << 8) + SUPER_OP_C_CSR_DST], &dst, 4);
    __builtin_memcpy((void *)&TAB_SUPER[(k << 8) + SUPER_OP_C_CSR32], &csr, 4);
  }
}

static inline void dispatch_write(volatile uint32_t *table, uint8_t index_hard, bus_t label) {
  table[index_hard >> 2] = label._0;
}

static volatile enum dbg_sta { DBG_STA_ILLEGAL, DBG_STA_OK } dbg_sta = DBG_STA_OK;
const char *dbg_sta_str(enum dbg_sta sta) {
  switch (sta) {
  case DBG_STA_ILLEGAL:
    return "DBG_STA_ILLEGAL";
  case DBG_STA_OK:
    return "DBG_STA_OK";
  default:
    __builtin_unreachable();
  }
}

static volatile uint32_t mark = 0;
#define MARK_(C, K) do{static volatile uint32_t C=K;ctx_emit(ctx,bus(&mark),bus(&C),4);}while(0)
#define MARK(K) MARK_(CONCAT(_MARK_, __COUNTER__), K)
static volatile uint32_t *dbg = 0;
// volatile _Alignas(256) __u32 RV_REGISTERS[33];
// volatile __u32 RV_PC;
// volatile __u32 RV_AMO_RESERVED = 0;

// Decode helpers

static volatile uint8_t funct3x4 = 0;
static volatile uint8_t funct7class = 0;
static volatile _Alignas(256) uint32_t RS[2];
static volatile uint32_t out;
static volatile uint32_t inst;

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

void riscv_generate_emulator(ctx_t *ctx) {
  volatile uint32_t *rs1 = &RS[0], *rs2=&RS[1];

  bus_t $all_ret_pcnext, $all_ret_direct;
  bus_t $illegal, $ecall_m, $ebreak;

  bus_t $alu_lui, $alu_auipc, $alu_jal, $alu_jalr, $alu_bcc, $alu_load, $alu_store,
      $alu_imm_arith,
      $alu_reg_arith,
      $alu_env,
      $alu_fence,
      $alu_atomic;

  bus_t $emu_custom_0, $emu_custom_1, $emu_custom_2, $emu_custom_3;
  bus_t $emu_ifetch;

  {
    static volatile uint32_t K_STATE_RUNNING = EMU_STATE_RUNNING;
    ctx_emit(ctx, bus(&RISCV->EMU_STATE), bus(&K_STATE_RUNNING), 4);
  }
  
  // --- Instruction fetcher
  {
    $emu_ifetch = ctx_label(ctx);

    ctx_emit(ctx, bus(&RISCV->PREV_PC), bus(&RISCV->LAST_PC), 4);
    ctx_emit(ctx, bus(&RISCV->LAST_PC), bus(&RISCV->PC), 4);
    bus_t $ifetch_load = ctx_label(ctx);
    // TODO: can just make RV_PC be the source field of that there instruction
    ctx_emit(ctx, bus_rel_fld($ifetch_load, +1, FLD_SRC, 0), bus(&RISCV->PC), 4);
    ctx_emit(ctx, bus(&inst), bus_null(), 4);
  }

  // --- First stage decoder
  // opcode lives in 6:0 for all types of instructions, and the lowest two bits
  // are 11 for normal instructions, so we can discard those. We use a special
  // OP5_MASK table for extracting said bits, and then branch via the
  // DISPATCH_STAGE_1 table.
  // XXX: If the low bits are _not_ 11, then we branch to $illegal instead.
  static volatile _Alignas(256) uint32_t DISPATCH_STAGE_1[64] = {};
  DUMP_TAB_AD(DISPATCH_STAGE_1);
  {
    static volatile _Alignas(256) uint8_t OP5_MASK[256];
    DUMP_TAB_AD(OP5_MASK);
    for (size_t i = 0; i < 256; i++)
      if ((i & 3) == 3)
        OP5_MASK[i] = i & 0x7c;
      else
        OP5_MASK[i] = 0b1111100;

    bus_t _0 = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld(_0, +1, FLD_SRC, 0), bus(&inst), 1);
    ctx_emit(ctx, bus_rel_fld(_0, +2, FLD_SRC, 0), bus(OP5_MASK), 1);
    ctx_emit(ctx, bus_rel_fld(_0, +3, FLD_NXT, 0), bus(DISPATCH_STAGE_1), 4);
    MARK(MARK_FST + 1);
    // ctx_emit(ctx, bus(&ZERO), bus(&ZERO), 4); // dummy op for branch control
  }

  // --- Common return point No. 1: PC INC4
  {
    static volatile _Alignas(256) uint32_t EXTERNAL_INTERRUPT_SWITCH[2];
    static volatile _Alignas(256) uint32_t TIMER_INTERRUPT_SWITCH[2];
    static volatile _Alignas(256) uint32_t DEBUG_TIMEOUT_SWITCH[2];
    static volatile _Alignas(256) uint32_t DEBUG_ILLEGAL_SWITCH[2];
    static volatile _Alignas(256) uint32_t THRI_DELAY_SWITCH[2];
    static volatile uint32_t K_FOUR = 4, tmp;
    static volatile uint32_t K_ZERO32 = 0;
    static volatile uint32_t K_STATE_DEBUG_TIMEOUT = EMU_STATE_DEBUG_TIMEOUT;
    static volatile uint32_t K_STATE_DEBUG_TRAP = EMU_STATE_DEBUG_TRAP;
    static volatile uint32_t K_MSTATUS_MIE = 1 << 3;
    static volatile uint32_t K_MSTATUS_TRAP_CLEAR = ~0x1888u;
    static volatile uint32_t K_MSTATUS_MPIE_SHIFT = 4;
    static volatile uint32_t K_MSTATUS_MPP_SHIFT = 11;
    static volatile uint32_t K_PRIV_M = 3;
    static volatile uint32_t K_THREE = 3;
    static volatile uint32_t K_MIE_MTIE = 1 << 7;
    static volatile uint32_t K_MIE_MEIE = 1 << 11;
    static volatile uint32_t K_MIP_MTIP = 1 << 7;
    static volatile uint32_t K_PLIC_UART_BIT = 1 << 10;
    static volatile uint32_t K_UART_IER_RDI = 1;
    static volatile uint32_t K_UART_IER_THRI = 2;
    static volatile uint32_t K_UART_LSR_DR = 1;
    static volatile uint32_t K_MCAUSE_ILLEGAL = 2;
    static volatile uint32_t K_MCAUSE_BREAKPOINT = 3;
    static volatile uint32_t K_MCAUSE_ECALL_U = 8;
    static volatile uint32_t K_MCAUSE_ECALL_M = 11;
    static volatile uint32_t K_MCAUSE_MTI = 0x80000007;
    static volatile uint32_t K_MCAUSE_MEI = 0x8000000b;
    static volatile uint32_t K_PLIC_UART_ID = 10;
    static volatile uint32_t plic_bit, plic_enable_bit, threshold_plus_one;
    static volatile uint32_t plic_pending_b, plic_enable_b, plic_priority_b;
    static volatile uint32_t uart_rx_ier, uart_rx_lsr;
    static volatile uint32_t uart_rx_ier_b, uart_rx_lsr_b, uart_rx_b;
    static volatile uint32_t uart_thri_ier, uart_thri_pending, uart_thri_delay_next;
    static volatile uint32_t uart_thri_ier_b, uart_thri_pending_b, uart_thri_delay_b;
    static volatile uint32_t uart_thri_delay_zero_b;
    static volatile uint32_t uart_thri_b;
    static volatile uint32_t mstatus_mie, mie_mtie, mie_meie, mstatus_mie_b, mie_mtie_b, mie_meie_b;
    static volatile uint32_t timer_match_b, timer_pending_b, timer_trap_b, external_trap_b;
    static volatile uint32_t debug_limit_b, debug_ge_b, debug_stop_b;
    static volatile uint32_t mstatus_tmp, trap_mpie, trap_mpp, mip_tmp;
    static volatile uint32_t ecall_m_b, ecall_delta, ecall_cause;

#define EMIT_MACHINE_TRAP_ENTRY()                                                                 \
  do {                                                                                             \
    stamp_and(ctx, bus(&trap_mpie), bus(&RISCV->MSTATUS), bus(&K_MSTATUS_MIE));                    \
    stamp_sll(ctx, bus(&trap_mpie), bus(&trap_mpie), bus(&K_MSTATUS_MPIE_SHIFT));                  \
    stamp_sll(ctx, bus(&trap_mpp), bus(&RISCV->PRIV), bus(&K_MSTATUS_MPP_SHIFT));                  \
    stamp_or(ctx, bus(&trap_mpie), bus(&trap_mpie), bus(&trap_mpp));                               \
    stamp_and(ctx, bus(&mstatus_tmp), bus(&RISCV->MSTATUS), bus(&K_MSTATUS_TRAP_CLEAR));           \
    stamp_or(ctx, bus(&mstatus_tmp), bus(&mstatus_tmp), bus(&trap_mpie));                          \
    ctx_emit(ctx, bus(&RISCV->MSTATUS), bus(&mstatus_tmp), 4);                                     \
    ctx_emit(ctx, bus(&RISCV->PRIV), bus(&K_PRIV_M), 4);                                           \
  } while (0)

    $all_ret_pcnext = ctx_label(ctx);
    stamp_add(ctx, bus(&tmp), bus(&RISCV->PC), bus(&K_FOUR));
    ctx_emit(ctx, bus(&RISCV->PC), bus(&tmp), 4);

    $all_ret_direct = ctx_label(ctx);
    stamp_inc(ctx, bus(&tmp), bus(&RISCV->CYCLEL));
    ctx_emit(ctx, bus(&RISCV->CYCLEL), bus(&tmp), 8);
    stamp_inc(ctx, bus(&tmp), bus(&RISCV->TIMERL));
    ctx_emit(ctx, bus(&RISCV->TIMERL), bus(&tmp), 8);

    stamp_eq_bww(ctx, bus(&debug_limit_b), bus(&RISCV->DEBUG_CYCLE_LIMIT), bus(&K_ZERO32),
                 CMP_X4 | CMP_INV);
    stamp_geu_bww(ctx, bus(&debug_ge_b), bus(&RISCV->CYCLEL), bus(&RISCV->DEBUG_CYCLE_LIMIT),
                  CMP_X4);
    stamp_and8(ctx, bus(&debug_stop_b), bus(&debug_limit_b), bus(&debug_ge_b));
    bus_t $debug_timeout_src_patch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($debug_timeout_src_patch, +1, FLD_SRC, 0), bus(&debug_stop_b), 1);
    bus_t $debug_timeout_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($debug_timeout_dispatch, +1, FLD_NXT, 0), bus(DEBUG_TIMEOUT_SWITCH), 4);
    MARK(MARK_FST + 4);

    bus_t $debug_timeout = ctx_label(ctx);
    ctx_emit(ctx, bus(&RISCV->EMU_STATE), bus(&K_STATE_DEBUG_TIMEOUT), 4);
    ctx_end(ctx);

    bus_t $interrupt_check = ctx_label(ctx);
    stamp_eq_bww(ctx, bus(&uart_thri_delay_b), bus(&RISCV->UART_IIR_THRI_DELAY), bus(&K_ZERO32),
                 CMP_X4 | CMP_INV);
    stamp_sub(ctx, bus(&uart_thri_delay_next), bus(&RISCV->UART_IIR_THRI_DELAY), bus(&K_ONE));
    bus_t $thri_delay_dispatch = bus_rel_cb(ctx_label(ctx), +1);
    ctx_emit(ctx, bus_rel_fld($thri_delay_dispatch, +0, FLD_SRC, 0), bus(&uart_thri_delay_b), 1);
    ctx_emit(ctx, bus_rel_fld($thri_delay_dispatch, +1, FLD_NXT, 0), bus(THRI_DELAY_SWITCH), 4);
    MARK(MARK_FST + 5);
    bus_t $thri_delay_done = ctx_label(ctx);
    stamp_and(ctx, bus(&plic_bit), bus(&RISCV->PLIC_PENDING), bus(&RISCV->PLIC_ENABLE));
    stamp_and(ctx, bus(&plic_bit), bus(&plic_bit), bus(&K_PLIC_UART_BIT));
    stamp_eq_bww(ctx, bus(&plic_pending_b), bus(&plic_bit), bus(&K_ZERO32), CMP_X4 | CMP_INV);
    stamp_and(ctx, bus(&plic_enable_bit), bus(&RISCV->PLIC_ENABLE), bus(&K_PLIC_UART_BIT));
    stamp_eq_bww(ctx, bus(&plic_enable_b), bus(&plic_enable_bit), bus(&K_ZERO32), CMP_X4 | CMP_INV);
    stamp_and(ctx, bus(&uart_thri_ier), bus(&RISCV->UART_IER), bus(&K_UART_IER_THRI));
    ctx_emit(ctx, bus(&uart_thri_pending), bus(&RISCV->UART_IIR_THRI_PENDING), 4);
    stamp_and(ctx, bus(&uart_rx_ier), bus(&RISCV->UART_IER), bus(&K_UART_IER_RDI));
    stamp_and(ctx, bus(&uart_rx_lsr), bus(&RISCV->UART_LSR), bus(&K_UART_LSR_DR));
    stamp_eq_bww(ctx, bus(&uart_rx_ier_b), bus(&uart_rx_ier), bus(&K_ZERO32), CMP_X4 | CMP_INV);
    stamp_eq_bww(ctx, bus(&uart_rx_lsr_b), bus(&uart_rx_lsr), bus(&K_ZERO32), CMP_X4 | CMP_INV);
    stamp_and8(ctx, bus(&uart_rx_b), bus(&uart_rx_ier_b), bus(&uart_rx_lsr_b));
    stamp_and8(ctx, bus(&uart_rx_b), bus(&uart_rx_b), bus(&plic_enable_b));
    stamp_eq_bww(ctx, bus(&uart_thri_ier_b), bus(&uart_thri_ier), bus(&K_ZERO32), CMP_X4 | CMP_INV);
    stamp_eq_bww(ctx, bus(&uart_thri_pending_b), bus(&uart_thri_pending), bus(&K_ZERO32),
                 CMP_X4 | CMP_INV);
    stamp_eq_bww(ctx, bus(&uart_thri_delay_zero_b), bus(&RISCV->UART_IIR_THRI_DELAY),
                 bus(&K_ZERO32), CMP_X4);
    stamp_and8(ctx, bus(&uart_thri_b), bus(&uart_thri_ier_b), bus(&uart_thri_pending_b));
    stamp_and8(ctx, bus(&uart_thri_b), bus(&uart_thri_b), bus(&uart_thri_delay_zero_b));
    stamp_and8(ctx, bus(&uart_thri_b), bus(&uart_thri_b), bus(&plic_enable_b));
    stamp_add(ctx, bus(&threshold_plus_one), bus(&RISCV->PLIC_THRESHOLD), bus(&K_ONE));
    stamp_geu_bww(ctx, bus(&plic_priority_b), bus(&RISCV->PLIC_UART_PRIORITY),
                  bus(&threshold_plus_one), CMP_X4);
    stamp_and(ctx, bus(&mstatus_mie), bus(&RISCV->MSTATUS), bus(&K_MSTATUS_MIE));
    stamp_eq_bww(ctx, bus(&mstatus_mie_b), bus(&mstatus_mie), bus(&K_ZERO32), CMP_X4 | CMP_INV);
    stamp_and(ctx, bus(&mie_mtie), bus(&RISCV->MIE), bus(&K_MIE_MTIE));
    stamp_eq_bww(ctx, bus(&mie_mtie_b), bus(&mie_mtie), bus(&K_ZERO32), CMP_X4 | CMP_INV);
    stamp_and(ctx, bus(&mie_meie), bus(&RISCV->MIE), bus(&K_MIE_MEIE));
    stamp_eq_bww(ctx, bus(&mie_meie_b), bus(&mie_meie), bus(&K_ZERO32), CMP_X4 | CMP_INV);
    stamp_or8(ctx, bus(&external_trap_b), bus(&plic_pending_b), bus(&uart_rx_b));
    stamp_or8(ctx, bus(&external_trap_b), bus(&external_trap_b), bus(&uart_thri_b));
    stamp_and8(ctx, bus(&external_trap_b), bus(&external_trap_b), bus(&plic_priority_b));
    stamp_and8(ctx, bus(&external_trap_b), bus(&external_trap_b), bus(&mstatus_mie_b));
    stamp_and8(ctx, bus(&external_trap_b), bus(&external_trap_b), bus(&mie_meie_b));

    stamp_eq_bww(ctx, bus(&timer_match_b), bus(&RISCV->TIMERMATCHL), bus(&K_ZERO32), CMP_X4 | CMP_INV);
    stamp_geu_bww(ctx, bus(&timer_pending_b), bus(&RISCV->TIMERL), bus(&RISCV->TIMERMATCHL), CMP_X4);
    stamp_and8(ctx, bus(&timer_pending_b), bus(&timer_pending_b), bus(&timer_match_b));
    stamp_and8(ctx, bus(&timer_trap_b), bus(&timer_pending_b), bus(&mstatus_mie_b));
    stamp_and8(ctx, bus(&timer_trap_b), bus(&timer_trap_b), bus(&mie_mtie_b));

    bus_t $external_interrupt_src_patch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($external_interrupt_src_patch, +1, FLD_SRC, 0), bus(&external_trap_b), 1);
    bus_t $external_interrupt_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($external_interrupt_dispatch, +1, FLD_NXT, 0), bus(EXTERNAL_INTERRUPT_SWITCH), 4);
    MARK(MARK_FST + 2);

    bus_t $external_interrupt_trap = ctx_label(ctx);
    ctx_emit(ctx, bus(&RISCV->MEPC), bus(&RISCV->PC), 4);
    ctx_emit(ctx, bus(&RISCV->MCAUSE), bus(&K_MCAUSE_MEI), 4);
    ctx_emit(ctx, bus(&RISCV->PLIC_CLAIM), bus(&K_PLIC_UART_ID), 4);
    stamp_or(ctx, bus(&mip_tmp), bus(&RISCV->MIP), bus(&K_MIE_MEIE));
    ctx_emit(ctx, bus(&RISCV->MIP), bus(&mip_tmp), 4);
    EMIT_MACHINE_TRAP_ENTRY();
    ctx_emit(ctx, bus(&RISCV->PC), bus(&RISCV->MTVEC), 4);
    ctx_branch(ctx, $emu_ifetch);

    bus_t $timer_interrupt_src_patch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($timer_interrupt_src_patch, +1, FLD_SRC, 0), bus(&timer_trap_b), 1);
    bus_t $timer_interrupt_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($timer_interrupt_dispatch, +1, FLD_NXT, 0), bus(TIMER_INTERRUPT_SWITCH), 4);
    MARK(MARK_FST + 3);

    bus_t $timer_interrupt_trap = ctx_label(ctx);
    ctx_emit(ctx, bus(&RISCV->MEPC), bus(&RISCV->PC), 4);
    ctx_emit(ctx, bus(&RISCV->MCAUSE), bus(&K_MCAUSE_MTI), 4);
    stamp_or(ctx, bus(&mip_tmp), bus(&RISCV->MIP), bus(&K_MIP_MTIP));
    ctx_emit(ctx, bus(&RISCV->MIP), bus(&mip_tmp), 4);
    EMIT_MACHINE_TRAP_ENTRY();
    ctx_emit(ctx, bus(&RISCV->PC), bus(&RISCV->MTVEC), 4);
    ctx_branch(ctx, $emu_ifetch);

    EXTERNAL_INTERRUPT_SWITCH[0] = $timer_interrupt_src_patch._0;
    EXTERNAL_INTERRUPT_SWITCH[1] = $external_interrupt_trap._0;
    TIMER_INTERRUPT_SWITCH[0] = $emu_ifetch._0;
    TIMER_INTERRUPT_SWITCH[1] = $timer_interrupt_trap._0;
    DEBUG_TIMEOUT_SWITCH[0] = $interrupt_check._0;
    DEBUG_TIMEOUT_SWITCH[1] = $debug_timeout._0;
    bus_t $thri_delay_dec = ctx_label(ctx);
    ctx_emit(ctx, bus(&RISCV->UART_IIR_THRI_DELAY), bus(&uart_thri_delay_next), 4);
    ctx_branch(ctx, $thri_delay_done);
    THRI_DELAY_SWITCH[0] = $thri_delay_done._0;
    THRI_DELAY_SWITCH[1] = $thri_delay_dec._0;

    $illegal = ctx_label(ctx);
    static enum dbg_sta K_ILLEGAL = DBG_STA_ILLEGAL;
    ctx_emit(ctx, bus(&dbg_sta), bus(&K_ILLEGAL), 4);
    ctx_emit(ctx, bus(&RISCV->MEPC), bus(&RISCV->PC), 4);
    ctx_emit(ctx, bus(&RISCV->MCAUSE), bus(&K_MCAUSE_ILLEGAL), 4);
    ctx_emit(ctx, bus(&RISCV->MTVAL), bus(&inst), 4);
    stamp_eq_bww(ctx, bus(&debug_limit_b), bus(&RISCV->DEBUG_CYCLE_LIMIT), bus(&K_ZERO32),
                 CMP_X4 | CMP_INV);
    bus_t $debug_illegal_src_patch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($debug_illegal_src_patch, +1, FLD_SRC, 0), bus(&debug_limit_b), 1);
    bus_t $debug_illegal_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($debug_illegal_dispatch, +1, FLD_NXT, 0), bus(DEBUG_ILLEGAL_SWITCH), 4);
    MARK(MARK_FST + 5);

    bus_t $debug_illegal = ctx_label(ctx);
    ctx_emit(ctx, bus(&RISCV->EMU_STATE), bus(&K_STATE_DEBUG_TRAP), 4);
    ctx_end(ctx);

    bus_t $illegal_trap = ctx_label(ctx);
    EMIT_MACHINE_TRAP_ENTRY();
    ctx_emit(ctx, bus(&RISCV->PC), bus(&RISCV->MTVEC), 4);
    ctx_branch(ctx, $emu_ifetch);
    DEBUG_ILLEGAL_SWITCH[0] = $illegal_trap._0;
    DEBUG_ILLEGAL_SWITCH[1] = $debug_illegal._0;

    $ebreak = ctx_label(ctx);
    ctx_emit(ctx, bus(&RISCV->MEPC), bus(&RISCV->PC), 4);
    ctx_emit(ctx, bus(&RISCV->MCAUSE), bus(&K_MCAUSE_BREAKPOINT), 4);
    ctx_emit(ctx, bus(&RISCV->MTVAL), bus(&inst), 4);
    EMIT_MACHINE_TRAP_ENTRY();
    ctx_emit(ctx, bus(&RISCV->PC), bus(&RISCV->MTVEC), 4);
    ctx_branch(ctx, $emu_ifetch);

    $ecall_m = ctx_label(ctx);
    ctx_emit(ctx, bus(&RISCV->MEPC), bus(&RISCV->PC), 4);
    stamp_eq_bww(ctx, bus(&ecall_m_b), bus(&RISCV->PRIV), bus(&K_ZERO32), CMP_INV);
    stamp_umul_www(ctx, bus(&ecall_delta), bus(&ecall_m_b), bus(&K_THREE));
    stamp_add(ctx, bus(&ecall_cause), bus(&K_MCAUSE_ECALL_U), bus(&ecall_delta));
    ctx_emit(ctx, bus(&RISCV->MCAUSE), bus(&ecall_cause), 4);
    ctx_emit(ctx, bus(&RISCV->MTVAL), bus(&K_ZERO32), 4);
    EMIT_MACHINE_TRAP_ENTRY();
    ctx_emit(ctx, bus(&RISCV->PC), bus(&RISCV->MTVEC), 4);
    ctx_branch(ctx, $emu_ifetch);

#undef EMIT_MACHINE_TRAP_ENTRY
  }

  // --- Second stage decoder: LUI : Opcode 01101 11
  {
    bus_t $join_lui = ctx_label(ctx);
    ctx_emit(ctx, bus(RISCV->REGS), bus(&out), 4);
    ctx_branch(ctx, $all_ret_pcnext);

    $alu_lui = ctx_label(ctx);

    bus_t _A = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld(_A, +1, FLD_SRC, 1), bus_(&inst, 0), 2);
    ctx_emit(ctx, bus_rel_fld(_A, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_A_RD), 1);
    ctx_emit(ctx, bus_rel_fld($join_lui, +0, FLD_DST, 0), bus(TAB_REGMAP_WR), 1);

    static volatile uint8_t MASK = 0xf0;
    ctx_emit(ctx, bus_(&out, 2), bus_(&inst, 2), 2);
    stamp_and8(ctx, bus_(&out, 1), bus_(&inst, 1), bus(&MASK));
    ctx_emit(ctx, bus_(&out, 0), bus(&K_ZERO), 1);

    ctx_branch(ctx, $join_lui);
  }

  // --- Second stage decoder: AUIPC : Opcode 00101 11
  {
    bus_t $join_auipc = ctx_label(ctx);
    ctx_emit(ctx, bus(RISCV->REGS), bus(&out), 4);
    ctx_branch(ctx, $all_ret_pcnext);

    $alu_auipc = ctx_label(ctx);

    bus_t _A = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld(_A, +1, FLD_SRC, 1), bus_(&inst, 0), 2);
    ctx_emit(ctx, bus_rel_fld(_A, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_A_RD), 1);
    ctx_emit(ctx, bus_rel_fld($join_auipc, +0, FLD_DST, 0), bus(TAB_REGMAP_WR), 1);

    static volatile uint8_t MASK = 0xf0;
    static volatile uint32_t tmp;
    ctx_emit(ctx, bus_(&tmp, 2), bus_(&inst, 2), 2);
    stamp_and8(ctx, bus_(&tmp, 1), bus_(&inst, 1), bus(&MASK));
    ctx_emit(ctx, bus_(&tmp, 0), bus(&K_ZERO), 1);
    stamp_add(ctx, bus(&out), bus(&tmp), bus(&RISCV->PC));
    ctx_branch(ctx, $join_auipc);
  }

  // --- Second stage decoder: JAL : Opcode 11011 11
  {
    bus_t $jal_writeback = ctx_label(ctx);
    ctx_emit(ctx, bus(RISCV->REGS), bus(&out), 4);
    static volatile uint32_t delta = 0;
    ctx_emit(ctx, bus(&delta), bus(TAB_JAL), 4);
    ctx_emit(ctx, bus(&out), bus(&RISCV->PC), 4);
    stamp_add(ctx, bus(&RISCV->PC), bus(&out), bus(&delta));
    ctx_branch(ctx, $all_ret_direct);

    $alu_jal = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($alu_jal, +1, FLD_SRC, 1), bus_(&inst, 0), 2);
    ctx_emit(ctx, bus_rel_fld($alu_jal, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_A_RD), 1);
    ctx_emit(ctx, bus_rel_fld($jal_writeback, +0, FLD_DST, 0), bus(TAB_REGMAP_WR), 1);

    static volatile uint32_t K_4 = 4;
    stamp_add(ctx, bus(&out), bus(&RISCV->PC), bus(&K_4));

    // Now the tricky part lol
    // 20 | 10:1 | 11 | 19:12
    // 20 gets replicated all the way down to 19, 11 gets moved down, and 10:1 get moved down
    static volatile uint32_t K_MASK = 0xf0;
    bus_t _0 = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($jal_writeback, +1, FLD_SRC, 1), bus_(&inst, 2), 2);
    stamp_and8(ctx, bus_rel_fld($jal_writeback, +1, FLD_SRC, 0), bus_(&inst, 1), bus(&K_MASK));
    ctx_branch(ctx, $jal_writeback);
  }

  // --- Second stage decoder: JALR : Opcode 11001 11
  {
    // t =pc+4; pc=(x[rs1]+sext(offset))&∼1; x[rd]=t
    bus_t $jalr_writeback = ctx_label(ctx);
    ctx_emit(ctx, bus(RISCV->REGS), bus(&out), 4);
    ctx_branch(ctx, $all_ret_direct);

    // Load rd backing address to $jalr_writeback
    $alu_jalr = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($alu_jalr, +1, FLD_SRC, 1), bus_(&inst, 0), 2);
    ctx_emit(ctx, bus_rel_fld($alu_jalr, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_A_RD), 1);
    ctx_emit(ctx, bus_rel_fld($jalr_writeback, +0, FLD_DST, 0), bus(TAB_REGMAP_WR), 1);

    static volatile uint32_t K_4 = 4;
    stamp_add(ctx, bus(&out), bus(&RISCV->PC), bus(&K_4));

    // Load rs1
    bus_t _B = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld(_B, +1, FLD_SRC, 1), bus_(&inst, 1), 2);
    ctx_emit(ctx, bus_rel_fld(_B, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_B_RS1_X4), 1);
    ctx_emit(ctx, bus(rs1), bus(RISCV->REGS), 4);

    // mask: need to zero the lowest bit
    static volatile uint32_t tmp = 0, target = 0, K_MASK = 0xfe;
    bus_t _C = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld(_C, +1, FLD_SRC, 1), bus_(&inst, 2), 2);
    ctx_emit(ctx, bus(&tmp), bus_(TAB_SUPER, SUPER_OP_IMM12_SEXTW), 4);
    stamp_add(ctx, bus(&target), bus(&tmp), bus(rs1));
    stamp_and8(ctx, bus(&target), bus(&target), bus(&K_MASK));
    ctx_emit(ctx, bus(&RISCV->PC), bus(&target), 4);
    ctx_branch(ctx, $jalr_writeback);
  }

  // --- Second stage decoder: Bcc : Opcode 11000 11
  // differentiated on funct3
  //  0->beq    1->bne    4->blt  5->bge  6->bltu  7->bgeu
  {
    // 12 | 10:5 @ 25,  4:1 | 11 @ 7
    // rs2, rs1, funct3, "rd"
    // okay, plan:
    // get xxx | 12 | x | 10:5 | xxxxx from top byte by table
    // get "rd" (4:1|11), then map to xxxx | 11 | xxxxxx | 4:1 | x by table
    // finally, OR the two together
    static volatile _Alignas(256) uint8_t TAB_BHI[1 << 10], TAB_BLO[1 << 9];
    for (int i = 0; i < 256; i++) {
      uint16_t b12 = (i & 0x80) >> 7;
      uint16_t b10_5 = (i & 0x7e) >> 1;
      uint16_t bhi = (b12 << 12) | (b10_5 << 5) | (b12 ? 0xf000 : 0);
      TAB_BHI[(0 << 8) + i] = bhi & 0xff;
      TAB_BHI[(1 << 8) + i] = bhi >> 8;
      TAB_BHI[(2 << 8) + i] = b12 ? 0xff : 0;
      TAB_BHI[(3 << 8) + i] = b12 ? 0xff : 0;
      uint16_t b4_1 = (i & 0x1e) >> 1;
      uint16_t b11 = i & 1;
      uint16_t blo = (b11 << 11) | (b4_1 << 1);
      TAB_BLO[(0 << 8) + i] = blo & 0xff;
      TAB_BLO[(1 << 8) + i] = blo >> 8;
    }
    bus_t $branch_taken = ctx_label(ctx);
    static volatile uint32_t tmp, bhi, blo;
    ctx_emit(ctx, bus(&tmp), bus(&RISCV->PC), 4);
    ctx_emit(ctx, bus_rel_fld($branch_taken, +2, FLD_SRC, 0), bus_(&inst, 3), 1);
    ctx_emit_strided(ctx, bus(&bhi), bus(TAB_BHI), 4, 1, _2d_src_stride(0xff));

    ctx_emit(ctx, bus_rel_fld($branch_taken, +4, FLD_SRC, 1), bus_(&inst, 0), 2);
    ctx_emit(ctx, bus_rel_fld($branch_taken, +5, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_A_RD), 1);
    ctx_emit_strided(ctx, bus(&blo), bus(TAB_BLO), 2, 1, _2d_src_stride(0xff));

    stamp_or8(ctx, bus_(&bhi, 0), bus_(&bhi, 0), bus_(&blo, 0));
    stamp_or8(ctx, bus_(&bhi, 1), bus_(&bhi, 1), bus_(&blo, 1));

    stamp_add(ctx, bus(&out), bus(&RISCV->PC), bus(&bhi));
    ctx_emit(ctx, bus(&RISCV->PC), bus(&out), 4);
    ctx_branch(ctx, $all_ret_direct);

    static volatile _Alignas(256) uint32_t BRANCH_SWITCH[2];
    BRANCH_SWITCH[0] = $all_ret_pcnext._0; // untaken
    BRANCH_SWITCH[1] = $branch_taken._0;

    bus_t $decider = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($decider, +1, FLD_NXT, 0), bus(BRANCH_SWITCH), 4);
    MARK(MARK_BCC + 7);

    // bus_t $beq, $bne, $blt, $bge, $bltu, $bgeu;
    bus_t $beq = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($decider, +0, FLD_SRC, 0), bus(rs1), bus(rs2), CMP_X4);
    ctx_branch(ctx, $decider);
    bus_t $bne = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($decider, +0, FLD_SRC, 0), bus(rs1), bus(rs2), CMP_X4 | CMP_INV);
    ctx_branch(ctx, $decider);
    bus_t $blt = ctx_label(ctx);
    stamp_ge_bww(ctx, bus_rel_fld($decider, +0, FLD_SRC, 0), bus(rs1), bus(rs2), CMP_X4 | CMP_INV);
    ctx_branch(ctx, $decider);
    bus_t $bge = ctx_label(ctx);
    stamp_ge_bww(ctx, bus_rel_fld($decider, +0, FLD_SRC, 0), bus(rs1), bus(rs2), CMP_X4);
    ctx_branch(ctx, $decider);
    bus_t $bltu = ctx_label(ctx);
    stamp_geu_bww(ctx, bus_rel_fld($decider, +0, FLD_SRC, 0), bus(rs1), bus(rs2), CMP_X4 | CMP_INV);
    ctx_branch(ctx, $decider);
    bus_t $bgeu = ctx_label(ctx);
    stamp_geu_bww(ctx, bus_rel_fld($decider, +0, FLD_SRC, 0), bus(rs1), bus(rs2), CMP_X4);
    ctx_branch(ctx, $decider);

    $alu_bcc = ctx_label(ctx);
    static volatile _Alignas(256) uint32_t DISPATCH_BCC[64];
    for (int i = 0; i < 64; i++)
      DISPATCH_BCC[i] = $illegal._0;
    DISPATCH_BCC[0] = $beq._0;
    DISPATCH_BCC[1] = $bne._0;
    DISPATCH_BCC[4] = $blt._0;
    DISPATCH_BCC[5] = $bge._0;
    DISPATCH_BCC[6] = $bltu._0;
    DISPATCH_BCC[7] = $bgeu._0;

    bus_t _A = ctx_label(ctx);
    // TODO: there's probably a way to use TDMODE striding to scatter the 2-byte windows across
    //       the CBs
    ctx_emit(ctx, bus_rel_fld(_A, +1, FLD_SRC, 1), bus_(&inst, 0), 2);
    ctx_emit(ctx, bus_rel_fld(_A, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_A_FUNCT3_X4), 1);
    ctx_emit(ctx, bus_rel_fld(_A, +8, FLD_NXT, 0), bus(DISPATCH_BCC), 4);

    bus_t _B = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld(_B, +1, FLD_SRC, 1), bus_(&inst, 1), 2);
    ctx_emit(ctx, bus_rel_fld(_B, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_B_RS1_X4), 1);
    ctx_emit(ctx, bus(rs1), bus(RISCV->REGS), 4);

    bus_t _C = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld(_C, +1, FLD_SRC, 1), bus_(&inst, 2), 2);
    ctx_emit(ctx, bus_rel_fld(_C, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_C_RS2_X4), 1);
    assert(ctx_at(ctx, bus_rel_cb(_A, +8))); // see _A+2
    ctx_emit(ctx, bus(rs2), bus(RISCV->REGS), 4);
  }

  // --- Second stage decoder: Loads : Opcode 00000 11
  // Dispatches on funct3:
  //  0->lb  1->lh  2->lw  4->lbu  5->lhu
  //
  // l<size><ext> <rd>, <simm12>(<rs1>)
  {
    static volatile _Alignas(0x100) uint8_t TAB_SEXT_BYTE[0x100];
    static volatile uint32_t LOAD_ADDR;
    static volatile uint32_t K_LOAD_DEBUG_MISS = 0;
    static volatile uint32_t K_LOAD_DEBUG_UART_IIR = 0x1008;
    static volatile uint32_t K_LOAD_DEBUG_UART_IIR_NONE = 0x1108;
    static volatile uint32_t K_LOAD_DEBUG_UART_IIR_THRI = 0x1208;
    for (int i = 0; i < 256; i++)
      TAB_SEXT_BYTE[i] = (i & 0x80) ? 0xff : 0;

    bus_t $lw_wb = ctx_label(ctx);
    ctx_emit(ctx, bus(RISCV->REGS), bus_null(), 4);
    ctx_branch(ctx, $all_ret_pcnext);
    bus_t $lbh_wb = ctx_label(ctx);
    ctx_emit(ctx, bus(RISCV->REGS), bus(&out), 4);
    ctx_branch(ctx, $all_ret_pcnext);

    bus_t $load_mmio_plic_uart_priority = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&RISCV->PLIC_UART_PRIORITY), 4);
    ctx_branch(ctx, $lbh_wb);
    bus_t $load_mmio_zero = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&K_ZERO), 4);
    ctx_branch(ctx, $lbh_wb);
    bus_t $load_mmio_plic_pending = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&RISCV->PLIC_PENDING), 4);
    ctx_branch(ctx, $lbh_wb);
    bus_t $load_mmio_plic_enable = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&RISCV->PLIC_ENABLE), 4);
    ctx_branch(ctx, $lbh_wb);
    bus_t $load_mmio_plic_threshold = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&RISCV->PLIC_THRESHOLD), 4);
    ctx_branch(ctx, $lbh_wb);
    static volatile uint32_t K_PLIC_UART_CLEAR = ~(1 << 10);
    static volatile uint32_t K_MEIP_CLEAR = ~(1 << 11);
    static volatile uint32_t plic_clear_tmp;
    bus_t $load_mmio_plic_claim = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&RISCV->PLIC_CLAIM), 4);
    stamp_and(ctx, bus(&plic_clear_tmp), bus(&RISCV->PLIC_PENDING), bus(&K_PLIC_UART_CLEAR));
    ctx_emit(ctx, bus(&RISCV->PLIC_PENDING), bus(&plic_clear_tmp), 4);
    stamp_and(ctx, bus(&plic_clear_tmp), bus(&RISCV->MIP), bus(&K_MEIP_CLEAR));
    ctx_emit(ctx, bus(&RISCV->MIP), bus(&plic_clear_tmp), 4);
    ctx_emit(ctx, bus(&RISCV->PLIC_CLAIM), bus(&K_ZERO), 4);
    ctx_branch(ctx, $lbh_wb);

    bus_t $load_mmio_clint_mtimecmp_lo = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&RISCV->TIMERMATCHL), 4);
    ctx_branch(ctx, $lbh_wb);
    bus_t $load_mmio_clint_mtimecmp_hi = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&RISCV->TIMERMATCH), 4);
    ctx_branch(ctx, $lbh_wb);
    bus_t $load_mmio_clint_mtime_lo = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&RISCV->TIMERL), 4);
    ctx_branch(ctx, $lbh_wb);
    bus_t $load_mmio_clint_mtime_hi = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&RISCV->TIMERH), 4);
    ctx_branch(ctx, $lbh_wb);
    static volatile uint32_t K_UART_IIR_NONE = 1;
    static volatile uint32_t K_UART_IIR_THRI = 2;
    static volatile uint32_t K_UART_IIR_RDI = 4;
    static volatile uint32_t K_UART_IER_RDI = 1;
    static volatile uint32_t K_UART_IER_THRI = 2;
    static volatile uint32_t K_UART_LSR_DR = 1;
    static volatile uint32_t K_UART_LSR_RX_CLEAR = ~1u;
    static volatile uint32_t K_UART_DLAB = 0x80, uart_load_dlab, uart_load_dlab_clear_b;
    static volatile uint32_t uart_lsr_tmp;
    static volatile uint32_t uart_iir_rdi_ier, uart_iir_rdi_lsr;
    static volatile uint32_t uart_iir_rdi_ier_b, uart_iir_rdi_lsr_b, uart_iir_rdi_b;
    static volatile uint32_t uart_iir_ier, uart_iir_ier_b, uart_iir_pending_b;
    static volatile uint32_t uart_iir_b, uart_iir_delay_zero_b;
    static volatile _Alignas(256) uint32_t LOAD_UART_RBR_DLAB_SWITCH[2];
    static volatile _Alignas(256) uint32_t LOAD_UART_IER_DLAB_SWITCH[2];
    bus_t $load_mmio_uart_rbr_rx = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&RISCV->UART_RBR), 4);
    stamp_and(ctx, bus(&uart_lsr_tmp), bus(&RISCV->UART_LSR), bus(&K_UART_LSR_RX_CLEAR));
    ctx_emit(ctx, bus(&RISCV->UART_LSR), bus(&uart_lsr_tmp), 4);
    ctx_branch(ctx, $lbh_wb);
    bus_t $load_mmio_uart_dll = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&RISCV->UART_DLL), 4);
    ctx_branch(ctx, $lbh_wb);
    bus_t $load_mmio_uart_rbr = ctx_label(ctx);
    stamp_and8(ctx, bus(&uart_load_dlab), bus(&RISCV->UART_LCR), bus(&K_UART_DLAB));
    stamp_eq_bww(ctx, bus(&uart_load_dlab_clear_b), bus(&uart_load_dlab), bus(&K_ZERO), CMP_X4);
    bus_t $load_uart_rbr_dlab_dispatch = bus_rel_cb(ctx_label(ctx), +1);
    ctx_emit(ctx, bus_rel_fld($load_uart_rbr_dlab_dispatch, +0, FLD_SRC, 0),
             bus(&uart_load_dlab_clear_b), 1);
	    ctx_emit(ctx, bus_rel_fld($load_uart_rbr_dlab_dispatch, +1, FLD_NXT, 0),
	             bus(LOAD_UART_RBR_DLAB_SWITCH), 4);
	    MARK(MARK_LOAD + 19);
	    LOAD_UART_RBR_DLAB_SWITCH[0] = $load_mmio_uart_dll._0;
	    LOAD_UART_RBR_DLAB_SWITCH[1] = $load_mmio_uart_rbr_rx._0;
    bus_t $load_mmio_uart_ier_live = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&RISCV->UART_IER), 4);
    ctx_branch(ctx, $lbh_wb);
    bus_t $load_mmio_uart_dlm = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&RISCV->UART_DLM), 4);
    ctx_branch(ctx, $lbh_wb);
    bus_t $load_mmio_uart_ier = ctx_label(ctx);
    stamp_and8(ctx, bus(&uart_load_dlab), bus(&RISCV->UART_LCR), bus(&K_UART_DLAB));
    stamp_eq_bww(ctx, bus(&uart_load_dlab_clear_b), bus(&uart_load_dlab), bus(&K_ZERO), CMP_X4);
    bus_t $load_uart_ier_dlab_dispatch = bus_rel_cb(ctx_label(ctx), +1);
    ctx_emit(ctx, bus_rel_fld($load_uart_ier_dlab_dispatch, +0, FLD_SRC, 0),
             bus(&uart_load_dlab_clear_b), 1);
	    ctx_emit(ctx, bus_rel_fld($load_uart_ier_dlab_dispatch, +1, FLD_NXT, 0),
	             bus(LOAD_UART_IER_DLAB_SWITCH), 4);
	    MARK(MARK_LOAD + 20);
	    LOAD_UART_IER_DLAB_SWITCH[0] = $load_mmio_uart_dlm._0;
	    LOAD_UART_IER_DLAB_SWITCH[1] = $load_mmio_uart_ier_live._0;
    bus_t $load_mmio_uart_iir_none = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&K_UART_IIR_NONE), 4);
#if RISCV_LOAD_DEBUG
    ctx_emit(ctx, bus(&RISCV->DEBUG_LOAD_KIND), bus(&K_LOAD_DEBUG_UART_IIR_NONE), 4);
    ctx_emit(ctx, bus(&RISCV->DEBUG_LOAD_OUT), bus(&K_UART_IIR_NONE), 4);
#endif
    ctx_branch(ctx, $lbh_wb);
    bus_t $load_mmio_uart_iir_rdi = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&K_UART_IIR_RDI), 4);
#if RISCV_LOAD_DEBUG
    ctx_emit(ctx, bus(&RISCV->DEBUG_LOAD_KIND), bus(&K_LOAD_DEBUG_UART_IIR), 4);
    ctx_emit(ctx, bus(&RISCV->DEBUG_LOAD_OUT), bus(&K_UART_IIR_RDI), 4);
#endif
    ctx_branch(ctx, $lbh_wb);
    bus_t $load_mmio_uart_iir_thri = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&K_UART_IIR_THRI), 4);
    ctx_emit(ctx, bus(&RISCV->UART_IIR_THRI_PENDING), bus(&K_ZERO), 4);
#if RISCV_LOAD_DEBUG
    ctx_emit(ctx, bus(&RISCV->DEBUG_LOAD_KIND), bus(&K_LOAD_DEBUG_UART_IIR_THRI), 4);
    ctx_emit(ctx, bus(&RISCV->DEBUG_LOAD_OUT), bus(&K_UART_IIR_THRI), 4);
#endif
    ctx_branch(ctx, $lbh_wb);
    static volatile _Alignas(256) uint32_t LOAD_UART_IIR_RDI_SWITCH[2];
    static volatile _Alignas(256) uint32_t LOAD_UART_IIR_THRI_SWITCH[2];
    bus_t $load_mmio_uart_iir_thri_check = ctx_label(ctx);
    bus_t $load_uart_iir_thri_dispatch = bus_rel_cb(ctx_label(ctx), +1);
    ctx_emit(ctx, bus_rel_fld($load_uart_iir_thri_dispatch, +0, FLD_SRC, 0), bus(&uart_iir_b), 1);
    ctx_emit(ctx, bus_rel_fld($load_uart_iir_thri_dispatch, +1, FLD_NXT, 0),
             bus(LOAD_UART_IIR_THRI_SWITCH), 4);
    MARK(MARK_LOAD + 18);
    LOAD_UART_IIR_THRI_SWITCH[0] = $load_mmio_uart_iir_none._0;
    LOAD_UART_IIR_THRI_SWITCH[1] = $load_mmio_uart_iir_thri._0;
    bus_t $load_mmio_uart_iir = ctx_label(ctx);
#if RISCV_LOAD_DEBUG
    ctx_emit(ctx, bus(&RISCV->DEBUG_LOAD_KIND), bus(&K_LOAD_DEBUG_UART_IIR), 4);
#endif
    stamp_and(ctx, bus(&uart_iir_rdi_ier), bus(&RISCV->UART_IER), bus(&K_UART_IER_RDI));
    stamp_and(ctx, bus(&uart_iir_rdi_lsr), bus(&RISCV->UART_LSR), bus(&K_UART_LSR_DR));
    stamp_eq_bww(ctx, bus(&uart_iir_rdi_ier_b), bus(&uart_iir_rdi_ier), bus(&K_ZERO),
                 CMP_X4 | CMP_INV);
    stamp_eq_bww(ctx, bus(&uart_iir_rdi_lsr_b), bus(&uart_iir_rdi_lsr), bus(&K_ZERO),
                 CMP_X4 | CMP_INV);
    stamp_and8(ctx, bus(&uart_iir_rdi_b), bus(&uart_iir_rdi_ier_b), bus(&uart_iir_rdi_lsr_b));
    stamp_and(ctx, bus(&uart_iir_ier), bus(&RISCV->UART_IER), bus(&K_UART_IER_THRI));
    stamp_eq_bww(ctx, bus(&uart_iir_ier_b), bus(&uart_iir_ier), bus(&K_ZERO), CMP_X4 | CMP_INV);
    stamp_eq_bww(ctx, bus(&uart_iir_pending_b), bus(&RISCV->UART_IIR_THRI_PENDING), bus(&K_ZERO),
                 CMP_X4 | CMP_INV);
    stamp_eq_bww(ctx, bus(&uart_iir_delay_zero_b), bus(&RISCV->UART_IIR_THRI_DELAY), bus(&K_ZERO),
                 CMP_X4);
    stamp_and8(ctx, bus(&uart_iir_b), bus(&uart_iir_ier_b), bus(&uart_iir_pending_b));
    stamp_and8(ctx, bus(&uart_iir_b), bus(&uart_iir_b), bus(&uart_iir_delay_zero_b));
    bus_t $load_uart_iir_rdi_dispatch = bus_rel_cb(ctx_label(ctx), +1);
    ctx_emit(ctx, bus_rel_fld($load_uart_iir_rdi_dispatch, +0, FLD_SRC, 0), bus(&uart_iir_rdi_b), 1);
    ctx_emit(ctx, bus_rel_fld($load_uart_iir_rdi_dispatch, +1, FLD_NXT, 0),
             bus(LOAD_UART_IIR_RDI_SWITCH), 4);
    MARK(MARK_LOAD + 17);
    LOAD_UART_IIR_RDI_SWITCH[0] = $load_mmio_uart_iir_thri_check._0;
    LOAD_UART_IIR_RDI_SWITCH[1] = $load_mmio_uart_iir_rdi._0;
	    bus_t $load_mmio_uart_lcr = ctx_label(ctx);
	    ctx_emit(ctx, bus(&out), bus(&RISCV->UART_LCR), 4);
	    ctx_branch(ctx, $lbh_wb);
    bus_t $load_mmio_uart_mcr = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&RISCV->UART_MCR), 4);
    ctx_branch(ctx, $lbh_wb);
	    bus_t $load_mmio_uart_lsr = ctx_label(ctx);
	    ctx_emit(ctx, bus(&out), bus(&RISCV->UART_LSR), 4);
	    ctx_branch(ctx, $lbh_wb);
	    bus_t $load_mmio_uart_msr = ctx_label(ctx);
	    ctx_emit(ctx, bus(&out), bus(&RISCV->UART_MSR), 4);
	    ctx_branch(ctx, $lbh_wb);
	    bus_t $load_mmio_uart_scr = ctx_label(ctx);
	    ctx_emit(ctx, bus(&out), bus(&RISCV->UART_SCR), 4);
	    ctx_branch(ctx, $lbh_wb);

	    static volatile _Alignas(256) uint32_t LOAD_PLIC_UART_PRIORITY_SWITCH[2];
    static volatile _Alignas(256) uint32_t LOAD_PLIC_PRIORITY_RANGE_SWITCH[2];
    static volatile _Alignas(256) uint32_t LOAD_PLIC_PENDING_SWITCH[2];
    static volatile _Alignas(256) uint32_t LOAD_PLIC_ENABLE_SWITCH[2];
    static volatile _Alignas(256) uint32_t LOAD_PLIC_THRESHOLD_SWITCH[2];
    static volatile _Alignas(256) uint32_t LOAD_PLIC_CLAIM_SWITCH[2];
    static volatile _Alignas(256) uint32_t LOAD_CLINT_MTIMECMP_LO_SWITCH[2];
    static volatile _Alignas(256) uint32_t LOAD_CLINT_MTIMECMP_HI_SWITCH[2];
    static volatile _Alignas(256) uint32_t LOAD_CLINT_MTIME_LO_SWITCH[2];
    static volatile _Alignas(256) uint32_t LOAD_CLINT_MTIME_HI_SWITCH[2];
    static volatile _Alignas(256) uint32_t LOAD_UART_RBR_SWITCH[2];
    static volatile _Alignas(256) uint32_t LOAD_UART_IER_SWITCH[2];
    static volatile _Alignas(256) uint32_t LOAD_UART_IIR_SWITCH[2];
	    static volatile _Alignas(256) uint32_t LOAD_UART_LCR_SWITCH[2];
	    static volatile _Alignas(256) uint32_t LOAD_UART_MCR_SWITCH[2];
	    static volatile _Alignas(256) uint32_t LOAD_UART_LSR_SWITCH[2];
	    static volatile _Alignas(256) uint32_t LOAD_UART_MSR_SWITCH[2];
	    static volatile _Alignas(256) uint32_t LOAD_UART_SCR_SWITCH[2];
	    static volatile uint32_t LOAD_MISS_LW, LOAD_MISS_LB, LOAD_MISS_LH, LOAD_MISS_LBU, LOAD_MISS_LHU;

    bus_t $load_plic_uart_priority_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($load_plic_uart_priority_dispatch, +1, FLD_NXT, 0),
             bus(LOAD_PLIC_UART_PRIORITY_SWITCH), 4);
    MARK(MARK_LOAD + 1);
    bus_t $load_plic_priority_range_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($load_plic_priority_range_dispatch, +1, FLD_NXT, 0),
             bus(LOAD_PLIC_PRIORITY_RANGE_SWITCH), 4);
    MARK(MARK_LOAD + 16);
    bus_t $load_plic_pending_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($load_plic_pending_dispatch, +1, FLD_NXT, 0), bus(LOAD_PLIC_PENDING_SWITCH),
             4);
    MARK(MARK_LOAD + 2);
    bus_t $load_plic_enable_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($load_plic_enable_dispatch, +1, FLD_NXT, 0), bus(LOAD_PLIC_ENABLE_SWITCH),
             4);
    MARK(MARK_LOAD + 3);
    bus_t $load_plic_threshold_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($load_plic_threshold_dispatch, +1, FLD_NXT, 0),
             bus(LOAD_PLIC_THRESHOLD_SWITCH), 4);
    MARK(MARK_LOAD + 4);
    bus_t $load_plic_claim_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($load_plic_claim_dispatch, +1, FLD_NXT, 0), bus(LOAD_PLIC_CLAIM_SWITCH),
             4);
    MARK(MARK_LOAD + 5);
    bus_t $load_clint_mtimecmp_lo_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($load_clint_mtimecmp_lo_dispatch, +1, FLD_NXT, 0),
             bus(LOAD_CLINT_MTIMECMP_LO_SWITCH), 4);
    MARK(MARK_LOAD + 6);
    bus_t $load_clint_mtimecmp_hi_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($load_clint_mtimecmp_hi_dispatch, +1, FLD_NXT, 0),
             bus(LOAD_CLINT_MTIMECMP_HI_SWITCH), 4);
    MARK(MARK_LOAD + 7);
    bus_t $load_clint_mtime_lo_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($load_clint_mtime_lo_dispatch, +1, FLD_NXT, 0),
             bus(LOAD_CLINT_MTIME_LO_SWITCH), 4);
    MARK(MARK_LOAD + 8);
    bus_t $load_clint_mtime_hi_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($load_clint_mtime_hi_dispatch, +1, FLD_NXT, 0),
             bus(LOAD_CLINT_MTIME_HI_SWITCH), 4);
    MARK(MARK_LOAD + 9);
    bus_t $load_uart_rbr_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($load_uart_rbr_dispatch, +1, FLD_NXT, 0), bus(LOAD_UART_RBR_SWITCH), 4);
    MARK(MARK_LOAD + 10);
    bus_t $load_uart_ier_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($load_uart_ier_dispatch, +1, FLD_NXT, 0), bus(LOAD_UART_IER_SWITCH), 4);
    MARK(MARK_LOAD + 11);
    bus_t $load_uart_iir_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($load_uart_iir_dispatch, +1, FLD_NXT, 0), bus(LOAD_UART_IIR_SWITCH), 4);
    MARK(MARK_LOAD + 12);
    bus_t $load_uart_lcr_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($load_uart_lcr_dispatch, +1, FLD_NXT, 0), bus(LOAD_UART_LCR_SWITCH), 4);
    MARK(MARK_LOAD + 13);
    bus_t $load_uart_mcr_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($load_uart_mcr_dispatch, +1, FLD_NXT, 0), bus(LOAD_UART_MCR_SWITCH), 4);
    MARK(MARK_LOAD + 14);
	    bus_t $load_uart_lsr_dispatch = ctx_label(ctx);
	    ctx_emit(ctx, bus_rel_fld($load_uart_lsr_dispatch, +1, FLD_NXT, 0), bus(LOAD_UART_LSR_SWITCH), 4);
	    MARK(MARK_LOAD + 15);
	    bus_t $load_uart_msr_dispatch = ctx_label(ctx);
	    ctx_emit(ctx, bus_rel_fld($load_uart_msr_dispatch, +1, FLD_NXT, 0), bus(LOAD_UART_MSR_SWITCH), 4);
	    MARK(MARK_LOAD + 21);
	    bus_t $load_uart_scr_dispatch = ctx_label(ctx);
	    ctx_emit(ctx, bus_rel_fld($load_uart_scr_dispatch, +1, FLD_NXT, 0), bus(LOAD_UART_SCR_SWITCH), 4);
	    MARK(MARK_LOAD + 22);

	    bus_t $load_check_plic_uart_priority = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($load_plic_uart_priority_dispatch, +0, FLD_SRC, 0), bus(&LOAD_ADDR),
                 bus(&MMIO_PLIC_UART_PRIORITY), CMP_X4);
    ctx_branch(ctx, $load_plic_uart_priority_dispatch);
    bus_t $load_check_plic_priority_range = ctx_label(ctx);
    static volatile uint32_t plic_priority_ge_base_b, plic_priority_lt_end_b, plic_priority_range_b;
    stamp_geu_bww(ctx, bus(&plic_priority_ge_base_b), bus(&LOAD_ADDR), bus(&MMIO_PLIC_PRIORITY_BASE),
                  CMP_X4);
    stamp_geu_bww(ctx, bus(&plic_priority_lt_end_b), bus(&LOAD_ADDR), bus(&MMIO_PLIC_PRIORITY_END),
                  CMP_X4 | CMP_INV);
    stamp_and8(ctx, bus(&plic_priority_range_b), bus(&plic_priority_ge_base_b),
               bus(&plic_priority_lt_end_b));
    ctx_emit(ctx, bus_rel_fld($load_plic_priority_range_dispatch, +0, FLD_SRC, 0),
             bus(&plic_priority_range_b), 1);
    ctx_branch(ctx, $load_plic_priority_range_dispatch);
    bus_t $load_check_plic_pending = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($load_plic_pending_dispatch, +0, FLD_SRC, 0), bus(&LOAD_ADDR),
                 bus(&MMIO_PLIC_PENDING), CMP_X4);
    ctx_branch(ctx, $load_plic_pending_dispatch);
    bus_t $load_check_plic_enable = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($load_plic_enable_dispatch, +0, FLD_SRC, 0), bus(&LOAD_ADDR),
                 bus(&MMIO_PLIC_ENABLE), CMP_X4);
    ctx_branch(ctx, $load_plic_enable_dispatch);
    bus_t $load_check_plic_threshold = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($load_plic_threshold_dispatch, +0, FLD_SRC, 0), bus(&LOAD_ADDR),
                 bus(&MMIO_PLIC_THRESHOLD), CMP_X4);
    ctx_branch(ctx, $load_plic_threshold_dispatch);
    bus_t $load_check_plic_claim = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($load_plic_claim_dispatch, +0, FLD_SRC, 0), bus(&LOAD_ADDR),
                 bus(&MMIO_PLIC_CLAIM), CMP_X4);
    ctx_branch(ctx, $load_plic_claim_dispatch);
    bus_t $load_check_clint_mtimecmp_lo = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($load_clint_mtimecmp_lo_dispatch, +0, FLD_SRC, 0), bus(&LOAD_ADDR),
                 bus(&MMIO_CLINT_MTIMECMP_LO), CMP_X4);
    ctx_branch(ctx, $load_clint_mtimecmp_lo_dispatch);
    bus_t $load_check_clint_mtimecmp_hi = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($load_clint_mtimecmp_hi_dispatch, +0, FLD_SRC, 0), bus(&LOAD_ADDR),
                 bus(&MMIO_CLINT_MTIMECMP_HI), CMP_X4);
    ctx_branch(ctx, $load_clint_mtimecmp_hi_dispatch);
    bus_t $load_check_clint_mtime_lo = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($load_clint_mtime_lo_dispatch, +0, FLD_SRC, 0), bus(&LOAD_ADDR),
                 bus(&MMIO_CLINT_MTIME_LO), CMP_X4);
    ctx_branch(ctx, $load_clint_mtime_lo_dispatch);
    bus_t $load_check_clint_mtime_hi = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($load_clint_mtime_hi_dispatch, +0, FLD_SRC, 0), bus(&LOAD_ADDR),
                 bus(&MMIO_CLINT_MTIME_HI), CMP_X4);
    ctx_branch(ctx, $load_clint_mtime_hi_dispatch);
    bus_t $load_check_uart_rbr = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($load_uart_rbr_dispatch, +0, FLD_SRC, 0), bus(&LOAD_ADDR),
                 bus(&MMIO_UART_THR_RBR), CMP_X4);
    ctx_branch(ctx, $load_uart_rbr_dispatch);
    bus_t $load_check_uart_ier = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($load_uart_ier_dispatch, +0, FLD_SRC, 0), bus(&LOAD_ADDR),
                 bus(&MMIO_UART_IER), CMP_X4);
    ctx_branch(ctx, $load_uart_ier_dispatch);
    bus_t $load_check_uart_iir = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($load_uart_iir_dispatch, +0, FLD_SRC, 0), bus(&LOAD_ADDR),
                 bus(&MMIO_UART_IIR_FCR), CMP_X4);
    ctx_branch(ctx, $load_uart_iir_dispatch);
    bus_t $load_check_uart_lcr = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($load_uart_lcr_dispatch, +0, FLD_SRC, 0), bus(&LOAD_ADDR),
                 bus(&MMIO_UART_LCR), CMP_X4);
    ctx_branch(ctx, $load_uart_lcr_dispatch);
    bus_t $load_check_uart_mcr = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($load_uart_mcr_dispatch, +0, FLD_SRC, 0), bus(&LOAD_ADDR),
                 bus(&MMIO_UART_MCR), CMP_X4);
    ctx_branch(ctx, $load_uart_mcr_dispatch);
	    bus_t $load_check_uart_lsr = ctx_label(ctx);
	    stamp_eq_bww(ctx, bus_rel_fld($load_uart_lsr_dispatch, +0, FLD_SRC, 0), bus(&LOAD_ADDR),
	                 bus(&MMIO_UART_LSR), CMP_X4);
	    ctx_branch(ctx, $load_uart_lsr_dispatch);
	    bus_t $load_check_uart_msr = ctx_label(ctx);
	    stamp_eq_bww(ctx, bus_rel_fld($load_uart_msr_dispatch, +0, FLD_SRC, 0), bus(&LOAD_ADDR),
	                 bus(&MMIO_UART_MSR), CMP_X4);
	    ctx_branch(ctx, $load_uart_msr_dispatch);
	    bus_t $load_check_uart_scr = ctx_label(ctx);
	    stamp_eq_bww(ctx, bus_rel_fld($load_uart_scr_dispatch, +0, FLD_SRC, 0), bus(&LOAD_ADDR),
	                 bus(&MMIO_UART_SCR), CMP_X4);
	    ctx_branch(ctx, $load_uart_scr_dispatch);

    LOAD_PLIC_UART_PRIORITY_SWITCH[0] = $load_check_plic_priority_range._0;
    LOAD_PLIC_UART_PRIORITY_SWITCH[1] = $load_mmio_plic_uart_priority._0;
    LOAD_PLIC_PRIORITY_RANGE_SWITCH[0] = $load_check_plic_pending._0;
    LOAD_PLIC_PRIORITY_RANGE_SWITCH[1] = $load_mmio_zero._0;
    LOAD_PLIC_PENDING_SWITCH[0] = $load_check_plic_enable._0;
    LOAD_PLIC_PENDING_SWITCH[1] = $load_mmio_plic_pending._0;
    LOAD_PLIC_ENABLE_SWITCH[0] = $load_check_plic_threshold._0;
    LOAD_PLIC_ENABLE_SWITCH[1] = $load_mmio_plic_enable._0;
    LOAD_PLIC_THRESHOLD_SWITCH[0] = $load_check_plic_claim._0;
    LOAD_PLIC_THRESHOLD_SWITCH[1] = $load_mmio_plic_threshold._0;
    LOAD_PLIC_CLAIM_SWITCH[0] = $load_check_clint_mtimecmp_lo._0;
    LOAD_PLIC_CLAIM_SWITCH[1] = $load_mmio_plic_claim._0;
    LOAD_CLINT_MTIMECMP_LO_SWITCH[0] = $load_check_clint_mtimecmp_hi._0;
    LOAD_CLINT_MTIMECMP_LO_SWITCH[1] = $load_mmio_clint_mtimecmp_lo._0;
    LOAD_CLINT_MTIMECMP_HI_SWITCH[0] = $load_check_clint_mtime_lo._0;
    LOAD_CLINT_MTIMECMP_HI_SWITCH[1] = $load_mmio_clint_mtimecmp_hi._0;
    LOAD_CLINT_MTIME_LO_SWITCH[0] = $load_check_clint_mtime_hi._0;
    LOAD_CLINT_MTIME_LO_SWITCH[1] = $load_mmio_clint_mtime_lo._0;
    LOAD_CLINT_MTIME_HI_SWITCH[0] = $load_check_uart_rbr._0;
    LOAD_CLINT_MTIME_HI_SWITCH[1] = $load_mmio_clint_mtime_hi._0;
    LOAD_UART_RBR_SWITCH[0] = $load_check_uart_ier._0;
    LOAD_UART_RBR_SWITCH[1] = $load_mmio_uart_rbr._0;
    LOAD_UART_IER_SWITCH[0] = $load_check_uart_iir._0;
    LOAD_UART_IER_SWITCH[1] = $load_mmio_uart_ier._0;
    LOAD_UART_IIR_SWITCH[0] = $load_check_uart_lcr._0;
	    LOAD_UART_IIR_SWITCH[1] = $load_mmio_uart_iir._0;
	    LOAD_UART_LCR_SWITCH[0] = $load_check_uart_mcr._0;
	    LOAD_UART_LCR_SWITCH[1] = $load_mmio_uart_lcr._0;
	    LOAD_UART_MCR_SWITCH[0] = $load_check_uart_lsr._0;
	    LOAD_UART_MCR_SWITCH[1] = $load_mmio_uart_mcr._0;
	    LOAD_UART_LSR_SWITCH[0] = $load_check_uart_msr._0;
	    LOAD_UART_MSR_SWITCH[0] = $load_check_uart_scr._0;
	    LOAD_UART_MSR_SWITCH[1] = $load_mmio_uart_msr._0;
	    LOAD_UART_SCR_SWITCH[1] = $load_mmio_uart_scr._0;

	    bus_t $lb_ld = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus_null(), 1);
    ctx_emit(ctx, bus_rel_fld($lb_ld, +2, FLD_SRC, 0), bus_(&out, 0), 1);
    ctx_emit_strided_with(ctx, DST_INC | _2DMODE, bus_(&out, 1), bus(TAB_SEXT_BYTE), 3, 1, 0);
    ctx_branch(ctx, $lbh_wb);

    bus_t $lh_ld = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus_null(), 2);
    ctx_emit(ctx, bus_rel_fld($lh_ld, +2, FLD_SRC, 0), bus_(&out, 1), 1);
    ctx_emit_strided_with(ctx, DST_INC | _2DMODE, bus_(&out, 2), bus(TAB_SEXT_BYTE), 2, 1, 0);
    ctx_branch(ctx, $lbh_wb);

    bus_t $lbu_ld = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus_null(), 1);
    ctx_emit(ctx, bus_rel_fld($lbu_ld, +1, FLD_SRC, 0), bus_(&out, 0), 1);
    ctx_emit(ctx, bus_(&out, 1), bus(&K_ZERO), 3);
    ctx_branch(ctx, $lbh_wb);

    bus_t $lhu_ld = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus_null(), 2);
    ctx_emit(ctx, bus_rel_fld($lhu_ld, +1, FLD_SRC, 0), bus_(&out, 0), 2);
    ctx_emit(ctx, bus_(&out, 2), bus(&K_ZERO), 2);
    ctx_branch(ctx, $lbh_wb);

    bus_t $lw_raw = ctx_label(ctx);
    stamp_add(ctx, bus_rel_fld($lw_wb, +0, FLD_SRC, 0), bus(rs1), bus(rs2));
    ctx_branch(ctx, $lw_wb);

    bus_t $lb_raw = ctx_label(ctx);
    stamp_add(ctx, bus_rel_fld($lb_ld, +0, FLD_SRC, 0), bus(rs1), bus(rs2));
    ctx_branch(ctx, $lb_ld);

    bus_t $lh_raw = ctx_label(ctx);
    stamp_add(ctx, bus_rel_fld($lh_ld, +0, FLD_SRC, 0), bus(rs1), bus(rs2));
    ctx_branch(ctx, $lh_ld);

    bus_t $lbu_raw = ctx_label(ctx);
    stamp_add(ctx, bus_rel_fld($lbu_ld, +0, FLD_SRC, 0), bus(rs1), bus(rs2));
    ctx_branch(ctx, $lbu_ld);

    bus_t $lhu_raw = ctx_label(ctx);
    stamp_add(ctx, bus_rel_fld($lhu_ld, +0, FLD_SRC, 0), bus(rs1), bus(rs2));
    ctx_branch(ctx, $lhu_ld);
    LOAD_MISS_LW = $lw_raw._0;
    LOAD_MISS_LB = $lb_raw._0;
    LOAD_MISS_LH = $lh_raw._0;
    LOAD_MISS_LBU = $lbu_raw._0;
    LOAD_MISS_LHU = $lhu_raw._0;

	    bus_t $lw = ctx_label(ctx);
	    ctx_emit(ctx, bus(&LOAD_UART_SCR_SWITCH[0]), bus(&LOAD_MISS_LW), 4);
	    stamp_add(ctx, bus(&LOAD_ADDR), bus(rs1), bus(rs2)); // rs1+imm12_sextw
#if RISCV_LOAD_DEBUG
    ctx_emit(ctx, bus(&RISCV->DEBUG_LOAD_ADDR), bus(&LOAD_ADDR), 4);
    ctx_emit(ctx, bus(&RISCV->DEBUG_LOAD_KIND), bus(&K_LOAD_DEBUG_MISS), 4);
    ctx_emit(ctx, bus(&RISCV->DEBUG_LOAD_OUT), bus(&K_ZERO), 4);
#endif
    ctx_branch(ctx, $load_check_plic_uart_priority);

	    bus_t $lb = ctx_label(ctx);
	    ctx_emit(ctx, bus(&LOAD_UART_SCR_SWITCH[0]), bus(&LOAD_MISS_LB), 4);
	    stamp_add(ctx, bus(&LOAD_ADDR), bus(rs1), bus(rs2));
	    ctx_branch(ctx, $load_check_plic_uart_priority);

	    bus_t $lh = ctx_label(ctx);
	    ctx_emit(ctx, bus(&LOAD_UART_SCR_SWITCH[0]), bus(&LOAD_MISS_LH), 4);
	    stamp_add(ctx, bus(&LOAD_ADDR), bus(rs1), bus(rs2));
	    ctx_branch(ctx, $load_check_plic_uart_priority);

	    bus_t $lbu = ctx_label(ctx);
	    ctx_emit(ctx, bus(&LOAD_UART_SCR_SWITCH[0]), bus(&LOAD_MISS_LBU), 4);
	    stamp_add(ctx, bus(&LOAD_ADDR), bus(rs1), bus(rs2));
	    ctx_branch(ctx, $load_check_plic_uart_priority);

	    bus_t $lhu = ctx_label(ctx);
	    ctx_emit(ctx, bus(&LOAD_UART_SCR_SWITCH[0]), bus(&LOAD_MISS_LHU), 4);
	    stamp_add(ctx, bus(&LOAD_ADDR), bus(rs1), bus(rs2));
	    ctx_branch(ctx, $load_check_plic_uart_priority);
	    LOAD_UART_LSR_SWITCH[1] = $load_mmio_uart_lsr._0;

    static volatile _Alignas(256) uint32_t DISPATCH_LOAD[64];
    for (int i = 0; i < 64; i++)
      DISPATCH_LOAD[i] = $illegal._0;
    DISPATCH_LOAD[0] = $lb._0;
    DISPATCH_LOAD[1] = $lh._0;
    DISPATCH_LOAD[2] = $lw._0;
    DISPATCH_LOAD[4] = $lbu._0;
    DISPATCH_LOAD[5] = $lhu._0;

    $alu_load = ctx_label(ctx);
    // Get <rd> and <funct3>*4
    bus_t _A = ctx_label(ctx);
    ctx_emit_strided_with(ctx, DST_INC | _2DMODE, bus_rel_fld(_A, +1, FLD_SRC, 1), bus_(&inst, 0), 2, 2,
                          _2d_dst_stride(30));
    ctx_emit(ctx, bus_rel_fld(_A, +3, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_A_FUNCT3_X4), 1);
    ctx_emit(ctx, bus_rel_fld(_A, +4, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_A_RD), 1);
    ctx_emit(ctx, bus_rel_fld(_A, +9, FLD_NXT, 0), bus(DISPATCH_LOAD), 4);
    ctx_emit_strided_with(ctx, DST_INC | _2DMODE, bus_rel_fld($lw_wb, +0, FLD_DST, 0), bus(TAB_REGMAP_WR),
                          2, 1, _2d_dst_stride(31));

    bus_t _B = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld(_B, +1, FLD_SRC, 1), bus_(&inst, 1), 2);
    ctx_emit(ctx, bus_rel_fld(_B, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_B_RS1_X4), 1);
    ctx_emit(ctx, bus(rs1), bus(RISCV->REGS), 4);

    // Get <imm12_sextw>
    bus_t _C = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld(_C, +1, FLD_SRC, 1), bus_(&inst, 2), 2);
    assert(ctx_at(ctx, bus_rel_cb(_A, +9)));
    ctx_emit(ctx, bus(rs2), bus_(TAB_SUPER, SUPER_OP_IMM12_SEXTW), 4);
  }

  // --- Second stage decoder: Stores : Opcode 01000 11
  // Dispatches on funct3:
  //  0->sb  1->sh  2->sw
  //
  // s<size> <rd>, <simm12>(<rs1>)
  {
    static volatile _Alignas(256) uint8_t TAB_IMM11_5[1 << 10];
    for (int i = 0; i < 256; i++) {
      uint16_t bhi = ((i & 0xfe) >> 1) << 5; // 11:5
      if (i & 0x80)
        bhi |= 0xf000;
      TAB_IMM11_5[(0 << 8) + i] = bhi & 0xff;
      TAB_IMM11_5[(1 << 8) + i] = bhi >> 8;
      TAB_IMM11_5[(2 << 8) + i] = (i & 0x80) ? 0xff : 0;
      TAB_IMM11_5[(3 << 8) + i] = (i & 0x80) ? 0xff : 0;
    }
    static volatile uint32_t TMP1 = 0, TMP2;

    bus_t $sb = ctx_label(ctx);
    ctx_emit(ctx, bus_null(), bus(rs2), 1);
    ctx_branch(ctx, $all_ret_pcnext);
    bus_t $sh = ctx_label(ctx);
    ctx_emit(ctx, bus_null(), bus(rs2), 2);
    ctx_branch(ctx, $all_ret_pcnext);
    bus_t $sw = ctx_label(ctx);
    ctx_emit(ctx, bus_null(), bus(rs2), 4);
    ctx_branch(ctx, $all_ret_pcnext);

	    bus_t $store_mmio_plic_uart_priority = ctx_label(ctx);
	    ctx_emit(ctx, bus(&RISCV->PLIC_UART_PRIORITY), bus(rs2), 4);
	    ctx_branch(ctx, $all_ret_pcnext);
	    bus_t $store_mmio_noop = ctx_label(ctx);
	    ctx_emit(ctx, bus(&CSR_SINK), bus(&K_ZERO), 4);
	    ctx_branch(ctx, $all_ret_pcnext);
    bus_t $store_mmio_plic_enable = ctx_label(ctx);
    ctx_emit(ctx, bus(&RISCV->PLIC_ENABLE), bus(rs2), 4);
    ctx_branch(ctx, $all_ret_pcnext);
    bus_t $store_mmio_plic_threshold = ctx_label(ctx);
    ctx_emit(ctx, bus(&RISCV->PLIC_THRESHOLD), bus(rs2), 4);
    ctx_branch(ctx, $all_ret_pcnext);
    static volatile uint32_t K_PLIC_UART_CLEAR = ~(1 << 10);
    static volatile uint32_t K_MEIP_CLEAR = ~(1 << 11);
    static volatile uint32_t plic_clear_tmp;
    bus_t $store_mmio_plic_claim = ctx_label(ctx);
    stamp_and(ctx, bus(&plic_clear_tmp), bus(&RISCV->PLIC_PENDING), bus(&K_PLIC_UART_CLEAR));
    ctx_emit(ctx, bus(&RISCV->PLIC_PENDING), bus(&plic_clear_tmp), 4);
    stamp_and(ctx, bus(&plic_clear_tmp), bus(&RISCV->MIP), bus(&K_MEIP_CLEAR));
    ctx_emit(ctx, bus(&RISCV->MIP), bus(&plic_clear_tmp), 4);
    ctx_emit(ctx, bus(&RISCV->PLIC_CLAIM), bus(&K_ZERO), 4);
    ctx_branch(ctx, $all_ret_pcnext);

    static volatile uint32_t K_MTIP_CLEAR = ~(1 << 7);
    bus_t $store_mmio_clint_mtimecmp_lo = ctx_label(ctx);
    ctx_emit(ctx, bus(&RISCV->TIMERMATCHL), bus(rs2), 4);
    stamp_and(ctx, bus(&plic_clear_tmp), bus(&RISCV->MIP), bus(&K_MTIP_CLEAR));
    ctx_emit(ctx, bus(&RISCV->MIP), bus(&plic_clear_tmp), 4);
    ctx_branch(ctx, $all_ret_pcnext);
    bus_t $store_mmio_clint_mtimecmp_hi = ctx_label(ctx);
    ctx_emit(ctx, bus(&RISCV->TIMERMATCH), bus(rs2), 4);
    stamp_and(ctx, bus(&plic_clear_tmp), bus(&RISCV->MIP), bus(&K_MTIP_CLEAR));
    ctx_emit(ctx, bus(&RISCV->MIP), bus(&plic_clear_tmp), 4);
    ctx_branch(ctx, $all_ret_pcnext);
    bus_t $store_mmio_clint_mtime_lo = ctx_label(ctx);
    ctx_emit(ctx, bus(&RISCV->TIMERL), bus(rs2), 4);
    ctx_branch(ctx, $all_ret_pcnext);
    bus_t $store_mmio_clint_mtime_hi = ctx_label(ctx);
    ctx_emit(ctx, bus(&RISCV->TIMERH), bus(rs2), 4);
    ctx_branch(ctx, $all_ret_pcnext);
    static volatile uint32_t K_UART_DLAB = 0x80, uart_dlab, uart_thr_tx_b;
    static volatile uint32_t K_UART_BYTE_MASK = 0xff, uart_store_byte;
    static volatile uint32_t K_UART_IER_MASK = 0x0f, uart_ier_masked;
    static volatile uint32_t K_UART_IER_THRI = 2;
	    static volatile uint32_t K_UART_LSR_THRE_TEMT = 0x60;
	    static volatile uint32_t K_UART_LSR_THRE = 0x20;
	    static volatile uint32_t K_UART_THRI_DELAY = 32768;
	    static volatile uint32_t uart_tx_lsr_tmp, uart_tx_ier_tmp, uart_tx_lsr_thre, uart_tx_irq_b;
    static volatile _Alignas(256) uint32_t STORE_UART_DLAB_SWITCH[2];
    static volatile _Alignas(256) uint32_t STORE_UART_IER_DLAB_SWITCH[2];
    static volatile _Alignas(256) uint32_t STORE_UART_TX_IRQ_SWITCH[2];
    bus_t $store_mmio_uart_thr = ctx_label(ctx);
    stamp_and(ctx, bus(&uart_store_byte), bus(rs2), bus(&K_UART_BYTE_MASK));
    stamp_and8(ctx, bus(&uart_dlab), bus(&RISCV->UART_LCR), bus(&K_UART_DLAB));
    stamp_eq_bww(ctx, bus(&uart_thr_tx_b), bus(&uart_dlab), bus(&K_ZERO), CMP_X4);
    bus_t $store_uart_dlab_dispatch = bus_rel_cb(ctx_label(ctx), +1);
    ctx_emit(ctx, bus_rel_fld($store_uart_dlab_dispatch, +0, FLD_SRC, 0),
             bus(&uart_thr_tx_b), 1);
	    ctx_emit(ctx, bus_rel_fld($store_uart_dlab_dispatch, +1, FLD_NXT, 0),
	             bus(STORE_UART_DLAB_SWITCH), 4);
	    MARK(MARK_STORE + 15);
	    bus_t $store_mmio_uart_dll = ctx_label(ctx);
    ctx_emit(ctx, bus(&RISCV->UART_DLL), bus(&uart_store_byte), 4);
    ctx_branch(ctx, $all_ret_pcnext);
	    bus_t $store_mmio_uart_thr_tx = ctx_label(ctx);
	    ctx_emit(ctx, (bus_t){0x7e215040}, bus(&uart_store_byte), 1);
    stamp_or(ctx, bus(&uart_tx_lsr_tmp), bus(&RISCV->UART_LSR), bus(&K_UART_LSR_THRE_TEMT));
    ctx_emit(ctx, bus(&RISCV->UART_LSR), bus(&uart_tx_lsr_tmp), 4);
    stamp_and(ctx, bus(&uart_tx_ier_tmp), bus(&RISCV->UART_IER), bus(&K_UART_IER_THRI));
    stamp_eq_bww(ctx, bus(&uart_tx_irq_b), bus(&uart_tx_ier_tmp), bus(&K_ZERO), CMP_X4 | CMP_INV);
    ctx_emit(ctx, bus_(&RISCV->UART_IIR_THRI_PENDING, 0), bus(&uart_tx_irq_b), 1);
    ctx_emit(ctx, bus(&RISCV->UART_IIR_THRI_DELAY), bus(&K_UART_THRI_DELAY), 4);
    ctx_branch(ctx, $all_ret_pcnext);
    STORE_UART_DLAB_SWITCH[0] = $store_mmio_uart_dll._0;
    STORE_UART_DLAB_SWITCH[1] = $store_mmio_uart_thr_tx._0;
    bus_t $store_mmio_uart_ier_live = ctx_label(ctx);
    stamp_and(ctx, bus(&uart_ier_masked), bus(&uart_store_byte), bus(&K_UART_IER_MASK));
    ctx_emit(ctx, bus(&RISCV->UART_IER), bus(&uart_ier_masked), 4);
    stamp_and(ctx, bus(&uart_tx_ier_tmp), bus(&uart_ier_masked), bus(&K_UART_IER_THRI));
    stamp_and(ctx, bus(&uart_tx_lsr_thre), bus(&RISCV->UART_LSR), bus(&K_UART_LSR_THRE));
    stamp_and(ctx, bus(&uart_tx_irq_b), bus(&uart_tx_ier_tmp), bus(&uart_tx_lsr_thre));
    stamp_eq_bww(ctx, bus(&uart_tx_irq_b), bus(&uart_tx_irq_b), bus(&K_ZERO), CMP_X4 | CMP_INV);
    ctx_emit(ctx, bus_(&RISCV->UART_IIR_THRI_PENDING, 0), bus(&uart_tx_irq_b), 1);
    ctx_emit(ctx, bus(&RISCV->UART_IIR_THRI_DELAY), bus(&K_UART_THRI_DELAY), 4);
    ctx_branch(ctx, $all_ret_pcnext);
    bus_t $store_mmio_uart_dlm = ctx_label(ctx);
    ctx_emit(ctx, bus(&RISCV->UART_DLM), bus(&uart_store_byte), 4);
    ctx_branch(ctx, $all_ret_pcnext);
    bus_t $store_mmio_uart_ier = ctx_label(ctx);
    stamp_and(ctx, bus(&uart_store_byte), bus(rs2), bus(&K_UART_BYTE_MASK));
    stamp_and8(ctx, bus(&uart_dlab), bus(&RISCV->UART_LCR), bus(&K_UART_DLAB));
    stamp_eq_bww(ctx, bus(&uart_thr_tx_b), bus(&uart_dlab), bus(&K_ZERO), CMP_X4);
    bus_t $store_uart_ier_dlab_dispatch = bus_rel_cb(ctx_label(ctx), +1);
    ctx_emit(ctx, bus_rel_fld($store_uart_ier_dlab_dispatch, +0, FLD_SRC, 0),
             bus(&uart_thr_tx_b), 1);
	    ctx_emit(ctx, bus_rel_fld($store_uart_ier_dlab_dispatch, +1, FLD_NXT, 0),
	             bus(STORE_UART_IER_DLAB_SWITCH), 4);
	    MARK(MARK_STORE + 16);
	    STORE_UART_IER_DLAB_SWITCH[0] = $store_mmio_uart_dlm._0;
    STORE_UART_IER_DLAB_SWITCH[1] = $store_mmio_uart_ier_live._0;
	    bus_t $store_mmio_uart_fcr = ctx_label(ctx);
	    ctx_emit(ctx, bus(&CSR_SINK), bus(&K_ZERO), 4);
	    ctx_branch(ctx, $all_ret_pcnext);
	    bus_t $store_mmio_uart_lcr = ctx_label(ctx);
	    stamp_and(ctx, bus(&uart_store_byte), bus(rs2), bus(&K_UART_BYTE_MASK));
	    ctx_emit(ctx, bus(&RISCV->UART_LCR), bus(&uart_store_byte), 4);
    ctx_branch(ctx, $all_ret_pcnext);
	    bus_t $store_mmio_uart_mcr = ctx_label(ctx);
	    stamp_and(ctx, bus(&uart_store_byte), bus(rs2), bus(&K_UART_BYTE_MASK));
	    ctx_emit(ctx, bus(&RISCV->UART_MCR), bus(&uart_store_byte), 4);
	    ctx_branch(ctx, $all_ret_pcnext);
	    bus_t $store_mmio_uart_msr = ctx_label(ctx);
	    ctx_emit(ctx, bus(&CSR_SINK), bus(&K_ZERO), 4);
	    ctx_branch(ctx, $all_ret_pcnext);
	    bus_t $store_mmio_uart_scr = ctx_label(ctx);
	    stamp_and(ctx, bus(&uart_store_byte), bus(rs2), bus(&K_UART_BYTE_MASK));
	    ctx_emit(ctx, bus(&RISCV->UART_SCR), bus(&uart_store_byte), 4);
	    ctx_branch(ctx, $all_ret_pcnext);

	    static volatile _Alignas(256) uint32_t STORE_PLIC_UART_PRIORITY_SWITCH[2];
    static volatile _Alignas(256) uint32_t STORE_PLIC_PRIORITY_RANGE_SWITCH[2];
    static volatile _Alignas(256) uint32_t STORE_PLIC_ENABLE_SWITCH[2];
    static volatile _Alignas(256) uint32_t STORE_PLIC_THRESHOLD_SWITCH[2];
    static volatile _Alignas(256) uint32_t STORE_PLIC_CLAIM_SWITCH[2];
    static volatile _Alignas(256) uint32_t STORE_CLINT_MTIMECMP_LO_SWITCH[2];
    static volatile _Alignas(256) uint32_t STORE_CLINT_MTIMECMP_HI_SWITCH[2];
    static volatile _Alignas(256) uint32_t STORE_CLINT_MTIME_LO_SWITCH[2];
    static volatile _Alignas(256) uint32_t STORE_CLINT_MTIME_HI_SWITCH[2];
    static volatile _Alignas(256) uint32_t STORE_UART_THR_SWITCH[2];
    static volatile _Alignas(256) uint32_t STORE_UART_IER_SWITCH[2];
	    static volatile _Alignas(256) uint32_t STORE_UART_FCR_SWITCH[2];
	    static volatile _Alignas(256) uint32_t STORE_UART_LCR_SWITCH[2];
	    static volatile _Alignas(256) uint32_t STORE_UART_MCR_SWITCH[2];
	    static volatile _Alignas(256) uint32_t STORE_UART_MSR_SWITCH[2];
	    static volatile _Alignas(256) uint32_t STORE_UART_SCR_SWITCH[2];
	    static volatile uint32_t STORE_MISS_SB, STORE_MISS_SH, STORE_MISS_SW;

    bus_t $store_plic_uart_priority_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($store_plic_uart_priority_dispatch, +1, FLD_NXT, 0),
             bus(STORE_PLIC_UART_PRIORITY_SWITCH), 4);
    MARK(MARK_STORE + 1);
    bus_t $store_plic_priority_range_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($store_plic_priority_range_dispatch, +1, FLD_NXT, 0),
             bus(STORE_PLIC_PRIORITY_RANGE_SWITCH), 4);
    MARK(MARK_STORE + 14);
    bus_t $store_plic_enable_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($store_plic_enable_dispatch, +1, FLD_NXT, 0), bus(STORE_PLIC_ENABLE_SWITCH),
             4);
    MARK(MARK_STORE + 2);
    bus_t $store_plic_threshold_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($store_plic_threshold_dispatch, +1, FLD_NXT, 0),
             bus(STORE_PLIC_THRESHOLD_SWITCH), 4);
    MARK(MARK_STORE + 3);
    bus_t $store_plic_claim_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($store_plic_claim_dispatch, +1, FLD_NXT, 0), bus(STORE_PLIC_CLAIM_SWITCH),
             4);
    MARK(MARK_STORE + 4);
    bus_t $store_clint_mtimecmp_lo_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($store_clint_mtimecmp_lo_dispatch, +1, FLD_NXT, 0),
             bus(STORE_CLINT_MTIMECMP_LO_SWITCH), 4);
    MARK(MARK_STORE + 5);
    bus_t $store_clint_mtimecmp_hi_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($store_clint_mtimecmp_hi_dispatch, +1, FLD_NXT, 0),
             bus(STORE_CLINT_MTIMECMP_HI_SWITCH), 4);
    MARK(MARK_STORE + 6);
    bus_t $store_clint_mtime_lo_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($store_clint_mtime_lo_dispatch, +1, FLD_NXT, 0),
             bus(STORE_CLINT_MTIME_LO_SWITCH), 4);
    MARK(MARK_STORE + 7);
    bus_t $store_clint_mtime_hi_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($store_clint_mtime_hi_dispatch, +1, FLD_NXT, 0),
             bus(STORE_CLINT_MTIME_HI_SWITCH), 4);
    MARK(MARK_STORE + 8);
    bus_t $store_uart_thr_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($store_uart_thr_dispatch, +1, FLD_NXT, 0), bus(STORE_UART_THR_SWITCH), 4);
    MARK(MARK_STORE + 9);
    bus_t $store_uart_ier_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($store_uart_ier_dispatch, +1, FLD_NXT, 0), bus(STORE_UART_IER_SWITCH), 4);
    MARK(MARK_STORE + 10);
    bus_t $store_uart_fcr_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($store_uart_fcr_dispatch, +1, FLD_NXT, 0), bus(STORE_UART_FCR_SWITCH), 4);
    MARK(MARK_STORE + 11);
    bus_t $store_uart_lcr_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($store_uart_lcr_dispatch, +1, FLD_NXT, 0), bus(STORE_UART_LCR_SWITCH), 4);
    MARK(MARK_STORE + 12);
	    bus_t $store_uart_mcr_dispatch = ctx_label(ctx);
	    ctx_emit(ctx, bus_rel_fld($store_uart_mcr_dispatch, +1, FLD_NXT, 0), bus(STORE_UART_MCR_SWITCH), 4);
	    MARK(MARK_STORE + 13);
	    bus_t $store_uart_msr_dispatch = ctx_label(ctx);
	    ctx_emit(ctx, bus_rel_fld($store_uart_msr_dispatch, +1, FLD_NXT, 0), bus(STORE_UART_MSR_SWITCH), 4);
	    MARK(MARK_STORE + 17);
	    bus_t $store_uart_scr_dispatch = ctx_label(ctx);
	    ctx_emit(ctx, bus_rel_fld($store_uart_scr_dispatch, +1, FLD_NXT, 0), bus(STORE_UART_SCR_SWITCH), 4);
	    MARK(MARK_STORE + 18);

    bus_t $store_check_plic_uart_priority = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($store_plic_uart_priority_dispatch, +0, FLD_SRC, 0), bus(&TMP2),
                 bus(&MMIO_PLIC_UART_PRIORITY), CMP_X4);
    ctx_branch(ctx, $store_plic_uart_priority_dispatch);
    bus_t $store_check_plic_priority_range = ctx_label(ctx);
    static volatile uint32_t store_plic_priority_ge_base_b, store_plic_priority_lt_end_b;
    static volatile uint32_t store_plic_priority_range_b;
    stamp_geu_bww(ctx, bus(&store_plic_priority_ge_base_b), bus(&TMP2),
                  bus(&MMIO_PLIC_PRIORITY_BASE), CMP_X4);
    stamp_geu_bww(ctx, bus(&store_plic_priority_lt_end_b), bus(&TMP2),
                  bus(&MMIO_PLIC_PRIORITY_END), CMP_X4 | CMP_INV);
    stamp_and8(ctx, bus(&store_plic_priority_range_b), bus(&store_plic_priority_ge_base_b),
               bus(&store_plic_priority_lt_end_b));
    ctx_emit(ctx, bus_rel_fld($store_plic_priority_range_dispatch, +0, FLD_SRC, 0),
             bus(&store_plic_priority_range_b), 1);
    ctx_branch(ctx, $store_plic_priority_range_dispatch);
    bus_t $store_check_plic_enable = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($store_plic_enable_dispatch, +0, FLD_SRC, 0), bus(&TMP2),
                 bus(&MMIO_PLIC_ENABLE), CMP_X4);
    ctx_branch(ctx, $store_plic_enable_dispatch);
    bus_t $store_check_plic_threshold = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($store_plic_threshold_dispatch, +0, FLD_SRC, 0), bus(&TMP2),
                 bus(&MMIO_PLIC_THRESHOLD), CMP_X4);
    ctx_branch(ctx, $store_plic_threshold_dispatch);
    bus_t $store_check_plic_claim = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($store_plic_claim_dispatch, +0, FLD_SRC, 0), bus(&TMP2),
                 bus(&MMIO_PLIC_CLAIM), CMP_X4);
    ctx_branch(ctx, $store_plic_claim_dispatch);
    bus_t $store_check_clint_mtimecmp_lo = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($store_clint_mtimecmp_lo_dispatch, +0, FLD_SRC, 0), bus(&TMP2),
                 bus(&MMIO_CLINT_MTIMECMP_LO), CMP_X4);
    ctx_branch(ctx, $store_clint_mtimecmp_lo_dispatch);
    bus_t $store_check_clint_mtimecmp_hi = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($store_clint_mtimecmp_hi_dispatch, +0, FLD_SRC, 0), bus(&TMP2),
                 bus(&MMIO_CLINT_MTIMECMP_HI), CMP_X4);
    ctx_branch(ctx, $store_clint_mtimecmp_hi_dispatch);
    bus_t $store_check_clint_mtime_lo = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($store_clint_mtime_lo_dispatch, +0, FLD_SRC, 0), bus(&TMP2),
                 bus(&MMIO_CLINT_MTIME_LO), CMP_X4);
    ctx_branch(ctx, $store_clint_mtime_lo_dispatch);
    bus_t $store_check_clint_mtime_hi = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($store_clint_mtime_hi_dispatch, +0, FLD_SRC, 0), bus(&TMP2),
                 bus(&MMIO_CLINT_MTIME_HI), CMP_X4);
    ctx_branch(ctx, $store_clint_mtime_hi_dispatch);
    bus_t $store_check_uart_thr = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($store_uart_thr_dispatch, +0, FLD_SRC, 0), bus(&TMP2),
                 bus(&MMIO_UART_THR_RBR), CMP_X4);
    ctx_branch(ctx, $store_uart_thr_dispatch);
    bus_t $store_check_uart_ier = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($store_uart_ier_dispatch, +0, FLD_SRC, 0), bus(&TMP2),
                 bus(&MMIO_UART_IER), CMP_X4);
    ctx_branch(ctx, $store_uart_ier_dispatch);
    bus_t $store_check_uart_fcr = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($store_uart_fcr_dispatch, +0, FLD_SRC, 0), bus(&TMP2),
                 bus(&MMIO_UART_IIR_FCR), CMP_X4);
    ctx_branch(ctx, $store_uart_fcr_dispatch);
    bus_t $store_check_uart_lcr = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($store_uart_lcr_dispatch, +0, FLD_SRC, 0), bus(&TMP2),
                 bus(&MMIO_UART_LCR), CMP_X4);
    ctx_branch(ctx, $store_uart_lcr_dispatch);
	    bus_t $store_check_uart_mcr = ctx_label(ctx);
	    stamp_eq_bww(ctx, bus_rel_fld($store_uart_mcr_dispatch, +0, FLD_SRC, 0), bus(&TMP2),
	                 bus(&MMIO_UART_MCR), CMP_X4);
	    ctx_branch(ctx, $store_uart_mcr_dispatch);
	    bus_t $store_check_uart_msr = ctx_label(ctx);
	    stamp_eq_bww(ctx, bus_rel_fld($store_uart_msr_dispatch, +0, FLD_SRC, 0), bus(&TMP2),
	                 bus(&MMIO_UART_MSR), CMP_X4);
	    ctx_branch(ctx, $store_uart_msr_dispatch);
	    bus_t $store_check_uart_scr = ctx_label(ctx);
	    stamp_eq_bww(ctx, bus_rel_fld($store_uart_scr_dispatch, +0, FLD_SRC, 0), bus(&TMP2),
	                 bus(&MMIO_UART_SCR), CMP_X4);
	    ctx_branch(ctx, $store_uart_scr_dispatch);

    STORE_PLIC_UART_PRIORITY_SWITCH[0] = $store_check_plic_priority_range._0;
    STORE_PLIC_UART_PRIORITY_SWITCH[1] = $store_mmio_plic_uart_priority._0;
    STORE_PLIC_PRIORITY_RANGE_SWITCH[0] = $store_check_plic_enable._0;
    STORE_PLIC_PRIORITY_RANGE_SWITCH[1] = $store_mmio_noop._0;
    STORE_PLIC_ENABLE_SWITCH[0] = $store_check_plic_threshold._0;
    STORE_PLIC_ENABLE_SWITCH[1] = $store_mmio_plic_enable._0;
    STORE_PLIC_THRESHOLD_SWITCH[0] = $store_check_plic_claim._0;
    STORE_PLIC_THRESHOLD_SWITCH[1] = $store_mmio_plic_threshold._0;
    STORE_PLIC_CLAIM_SWITCH[0] = $store_check_clint_mtimecmp_lo._0;
    STORE_PLIC_CLAIM_SWITCH[1] = $store_mmio_plic_claim._0;
    STORE_CLINT_MTIMECMP_LO_SWITCH[0] = $store_check_clint_mtimecmp_hi._0;
    STORE_CLINT_MTIMECMP_LO_SWITCH[1] = $store_mmio_clint_mtimecmp_lo._0;
    STORE_CLINT_MTIMECMP_HI_SWITCH[0] = $store_check_clint_mtime_lo._0;
    STORE_CLINT_MTIMECMP_HI_SWITCH[1] = $store_mmio_clint_mtimecmp_hi._0;
    STORE_CLINT_MTIME_LO_SWITCH[0] = $store_check_clint_mtime_hi._0;
    STORE_CLINT_MTIME_LO_SWITCH[1] = $store_mmio_clint_mtime_lo._0;
    STORE_CLINT_MTIME_HI_SWITCH[0] = $store_check_uart_thr._0;
    STORE_CLINT_MTIME_HI_SWITCH[1] = $store_mmio_clint_mtime_hi._0;
    STORE_UART_THR_SWITCH[0] = $store_check_uart_ier._0;
    STORE_UART_THR_SWITCH[1] = $store_mmio_uart_thr._0;
    STORE_UART_IER_SWITCH[0] = $store_check_uart_fcr._0;
    STORE_UART_IER_SWITCH[1] = $store_mmio_uart_ier._0;
	    STORE_UART_FCR_SWITCH[0] = $store_check_uart_lcr._0;
	    STORE_UART_FCR_SWITCH[1] = $store_mmio_uart_fcr._0;
	    STORE_UART_LCR_SWITCH[0] = $store_check_uart_mcr._0;
	    STORE_UART_LCR_SWITCH[1] = $store_mmio_uart_lcr._0;
	    STORE_UART_MCR_SWITCH[0] = $store_check_uart_msr._0;
	    STORE_UART_MCR_SWITCH[1] = $store_mmio_uart_mcr._0;
	    STORE_UART_MSR_SWITCH[0] = $store_check_uart_scr._0;
	    STORE_UART_MSR_SWITCH[1] = $store_mmio_uart_msr._0;
	    STORE_UART_SCR_SWITCH[1] = $store_mmio_uart_scr._0;
	    STORE_MISS_SB = $sb._0;
	    STORE_MISS_SH = $sh._0;
	    STORE_MISS_SW = $sw._0;

	    bus_t $sb_checked = ctx_label(ctx);
	    ctx_emit(ctx, bus(&STORE_UART_SCR_SWITCH[0]), bus(&STORE_MISS_SB), 4);
	    ctx_branch(ctx, $store_check_plic_uart_priority);

	    bus_t $sh_checked = ctx_label(ctx);
	    ctx_emit(ctx, bus(&STORE_UART_SCR_SWITCH[0]), bus(&STORE_MISS_SH), 4);
	    ctx_branch(ctx, $store_check_plic_uart_priority);

	    bus_t $sw_checked = ctx_label(ctx);
	    ctx_emit(ctx, bus(&STORE_UART_SCR_SWITCH[0]), bus(&STORE_MISS_SW), 4);
	    ctx_branch(ctx, $store_check_plic_uart_priority);

    static volatile _Alignas(256) uint32_t DISPATCH_STORE[64];
    for (int i = 0; i < 64; i++)
      DISPATCH_STORE[i] = $illegal._0;
    DISPATCH_STORE[0] = $sb_checked._0;
    DISPATCH_STORE[1] = $sh_checked._0;
    DISPATCH_STORE[2] = $sw_checked._0;

    bus_t $s_dispatch = ctx_label(ctx);
    ctx_emit_strided_with(ctx, DST_INC | _2DMODE, bus_rel_fld($sb, +0, FLD_DST, 0), bus(&TMP2), 3, 4,
                          _2d_dst_stride(28));
    // BRANCH: VIA DISPATCH_STORE

    $alu_store = ctx_label(ctx);
    // Get "<rd>" (really imm[4:0]) and <funct3>*4
    bus_t _A = ctx_label(ctx);
    ctx_emit_strided_with(ctx, DST_INC | _2DMODE, bus_rel_fld(_A, +1, FLD_SRC, 1), bus_(&inst, 0), 2, 2,
                          _2d_dst_stride(30));
    ctx_emit(ctx, bus_rel_fld(_A, +3, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_A_FUNCT3_X4), 1);
    ctx_emit(ctx, bus(&TMP1), bus_(TAB_SUPER, SUPER_OP_A_RD), 1);
    ctx_emit(ctx, bus_rel_fld($s_dispatch, +0, FLD_NXT, 0), bus(DISPATCH_STORE), 4);

    // rs1
    bus_t _B = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld(_B, +1, FLD_SRC, 1), bus_(&inst, 1), 2);
    ctx_emit(ctx, bus_rel_fld(_B, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_B_RS1_X4), 1);
    ctx_emit(ctx, bus(rs1), bus(RISCV->REGS), 4);

    // rs2, imm[11:5]
    bus_t _C = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld(_C, +1, FLD_SRC, 1), bus_(&inst, 2), 2);
    ctx_emit(ctx, bus_rel_fld(_C, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_C_RS2_X4), 1);
    ctx_emit(ctx, bus(rs2), bus(RISCV->REGS), 4);
    ctx_emit(ctx, bus_rel_fld(_C, +4, FLD_SRC, 0), bus_(&inst, 3), 1);
    ctx_emit_strided(ctx, bus(&out), bus(TAB_IMM11_5), 4, 1, _2d_src_stride(0xff));
    stamp_or8(ctx, bus_(&out, 0), bus_(&out, 0), bus_(&TMP1, 0));
    stamp_or8(ctx, bus_(&out, 1), bus_(&out, 1), bus_(&TMP1, 1));
    stamp_add(ctx, bus(&TMP2), bus(rs1), bus(&out));

	    ctx_branch(ctx, $s_dispatch);
  }

  // --- Second stage decoder: FENCE (.I) : Opcode 00011 11
  // It turns out that FENCE.I is effectively a big ol' NOP on our architecture, on account of how
  // Literally Everything Is Memory.
  // Plain FENCE I'm less sure of, but I figure it's probably okay?
  {
    $alu_fence = ctx_label(ctx);
    MARK(MARK_FENCE + 1);
    ctx_branch(ctx, $all_ret_pcnext);
  }

  // --- Second stage decoder: Register Arithmetic : Opcode 01100 11
  // add,sub,xor,or,and,sll,srl,sra,slt,sltu all have opcode 01100 11; they're
  // differentiated by funct3 (primarily) and funct7 (only for add/sub and
  // srl/sra)
  // --- Second stage decoder: Immediate Arithmetic : Opcode 00100 11
  {
    static volatile _Alignas(256) uint32_t DISPATCH_REG_ARITH[64] = {};
    static volatile _Alignas(256) uint32_t DISPATCH_IMM_ARITH[64] = {};
    for (int i = 0; i < 64; i++)
      DISPATCH_REG_ARITH[i] = DISPATCH_IMM_ARITH[i] = $illegal._0;
    DUMP_TAB_AD(DISPATCH_REG_ARITH);
    DUMP_TAB_AD(DISPATCH_IMM_ARITH);

    bus_t $join_reg_arith = ctx_label(ctx);
    ctx_emit(ctx, bus(RISCV->REGS), bus(&out), 4); // fixed: see reg_fwd+0
    ctx_branch(ctx, $all_ret_pcnext);

    bus_t $add = ctx_label(ctx);
    stamp_add(ctx, bus(&out), bus(rs1), bus(rs2));
    ctx_branch(ctx, $join_reg_arith);
    bus_t $sub = ctx_label(ctx);
    stamp_sub(ctx, bus(&out), bus(rs1), bus(rs2));
    ctx_branch(ctx, $join_reg_arith);
    bus_t $xor = ctx_label(ctx);
    stamp_xor(ctx, bus(&out), bus(rs1), bus(rs2));
    ctx_branch(ctx, $join_reg_arith);
    bus_t $or = ctx_label(ctx);
    stamp_or(ctx, bus(&out), bus(rs1), bus(rs2));
    ctx_branch(ctx, $join_reg_arith);
    bus_t $and = ctx_label(ctx);
    stamp_and(ctx, bus(&out), bus(rs1), bus(rs2));
    ctx_branch(ctx, $join_reg_arith);
    bus_t $sll = ctx_label(ctx);
    stamp_sll(ctx, bus(&out), bus(rs1), bus(rs2));
    ctx_branch(ctx, $join_reg_arith);
    bus_t $srl = ctx_label(ctx);
    stamp_srl(ctx, bus(&out), bus(rs1), bus(rs2));
    ctx_branch(ctx, $join_reg_arith);
    bus_t $sra = ctx_label(ctx);
    stamp_sra(ctx, bus(&out), bus(rs1), bus(rs2));
    ctx_branch(ctx, $join_reg_arith);

    static volatile uint32_t slt_tmp;
    static volatile _Alignas(256) uint8_t SLT_SIGN[256];
    for (int i = 0; i < 256; i++)
      SLT_SIGN[i] = (i & 0x80) ? 1 : 0;
    bus_t $slt = ctx_label(ctx);
    // TODO: Switch to stamp_ge_bww
    stamp_neg(ctx, bus(rs2), bus(rs2));
    stamp_add(ctx, bus(&slt_tmp), bus(rs1), bus(rs2));
    bus_t _slt_0 = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld(_slt_0, +1, FLD_SRC, 0), bus_(&slt_tmp, 3), 1);
    ctx_emit(ctx, bus(&out), bus(SLT_SIGN), 1);
    ctx_emit(ctx, bus_(&out, 1), bus(&K_ZERO), 3);
    ctx_branch(ctx, $join_reg_arith);
    bus_t $sltu = ctx_label(ctx);
    stamp_geu_bww(ctx, bus(&out), bus(rs1), bus(rs2), CMP_INV);
    ctx_emit(ctx, bus_(&out, 1), bus(&K_ZERO), 3);
    ctx_branch(ctx, $join_reg_arith);

    static volatile uint32_t div_tmp;
    static volatile uint32_t mul_sign_a, mul_sign_b, mul_adj_a, mul_adj_b;
    static volatile uint32_t K_SHIFT_31 = 31;
    static volatile uint64_t mul_tmp;

    bus_t $mul = ctx_label(ctx);
    stamp_umul_www(ctx, bus(&out), bus(rs1), bus(rs2));
    ctx_branch(ctx, $join_reg_arith);

    bus_t $mulh = ctx_label(ctx);
    stamp_umul_qww(ctx, bus(&mul_tmp), bus(rs1), bus(rs2));
    stamp_srl(ctx, bus(&mul_sign_a), bus(rs1), bus(&K_SHIFT_31));
    stamp_srl(ctx, bus(&mul_sign_b), bus(rs2), bus(&K_SHIFT_31));
    stamp_umul_www(ctx, bus(&mul_adj_a), bus(&mul_sign_a), bus(rs2));
    stamp_umul_www(ctx, bus(&mul_adj_b), bus(&mul_sign_b), bus(rs1));
    stamp_sub(ctx, bus(&out), bus_add(bus(&mul_tmp), 4), bus(&mul_adj_a));
    stamp_sub(ctx, bus(&out), bus(&out), bus(&mul_adj_b));
    ctx_branch(ctx, $join_reg_arith);

    bus_t $mulhsu = ctx_label(ctx);
    stamp_umul_qww(ctx, bus(&mul_tmp), bus(rs1), bus(rs2));
    stamp_srl(ctx, bus(&mul_sign_a), bus(rs1), bus(&K_SHIFT_31));
    stamp_umul_www(ctx, bus(&mul_adj_a), bus(&mul_sign_a), bus(rs2));
    stamp_sub(ctx, bus(&out), bus_add(bus(&mul_tmp), 4), bus(&mul_adj_a));
    ctx_branch(ctx, $join_reg_arith);

    bus_t $mulhu = ctx_label(ctx);
    stamp_umul_qww(ctx, bus(&mul_tmp), bus(rs1), bus(rs2));
    ctx_emit(ctx, bus(&out), bus_add(bus(&mul_tmp), 4), 4);
    ctx_branch(ctx, $join_reg_arith);

    bus_t $div = ctx_label(ctx);
    stamp_divmod_www(ctx, bus(&out), bus(&div_tmp), bus(rs1), bus(rs2));
    ctx_branch(ctx, $join_reg_arith);

    bus_t $rem = ctx_label(ctx);
    stamp_divmod_www(ctx, bus(&div_tmp), bus(&out), bus(rs1), bus(rs2));
    ctx_branch(ctx, $join_reg_arith);

    bus_t $divu = ctx_label(ctx);
    udivmod_exits_t divu_exits = stamp_udivmod_www(ctx, bus(&out), bus(&div_tmp), bus(rs1), bus(rs2));
    udivmod_exits_set_next(divu_exits, $join_reg_arith);

    bus_t $remu = ctx_label(ctx);
    udivmod_exits_t remu_exits = stamp_udivmod_www(ctx, bus(&div_tmp), bus(&out), bus(rs1), bus(rs2));
    udivmod_exits_set_next(remu_exits, $join_reg_arith);

#define REG_ARITH_CLASS_BASE 0x00
#define REG_ARITH_CLASS_M 0x40
#define REG_ARITH_CLASS_ALT 0x20
#define REG_ARITH_CLASS_ILLEGAL 0xfc
#define dispatch_reg_arith(f3, cls, lbl) dispatch_write(DISPATCH_REG_ARITH, ((f3) << 2) | (cls), lbl)
    dispatch_reg_arith(0, 0, $add);
    dispatch_reg_arith(0, REG_ARITH_CLASS_ALT, $sub);
    dispatch_reg_arith(4, 0, $xor);
    dispatch_reg_arith(6, 0, $or);
    dispatch_reg_arith(7, 0, $and);
    dispatch_reg_arith(1, 0, $sll);
    dispatch_reg_arith(5, 0, $srl);
    dispatch_reg_arith(5, REG_ARITH_CLASS_ALT, $sra);
    dispatch_reg_arith(2, 0, $slt);
    dispatch_reg_arith(3, 0, $sltu);
    dispatch_reg_arith(0, REG_ARITH_CLASS_M, $mul);
    dispatch_reg_arith(1, REG_ARITH_CLASS_M, $mulh);
    dispatch_reg_arith(2, REG_ARITH_CLASS_M, $mulhsu);
    dispatch_reg_arith(3, REG_ARITH_CLASS_M, $mulhu);
    dispatch_reg_arith(4, REG_ARITH_CLASS_M, $div);
    dispatch_reg_arith(5, REG_ARITH_CLASS_M, $divu);
    dispatch_reg_arith(6, REG_ARITH_CLASS_M, $rem);
    dispatch_reg_arith(7, REG_ARITH_CLASS_M, $remu);

    static volatile _Alignas(256) uint8_t REG_FUNCT7_CLASS[256];
    for(int i=0;i<256;i++) REG_FUNCT7_CLASS[i] = REG_ARITH_CLASS_ILLEGAL;
    REG_FUNCT7_CLASS[0x00] = REG_ARITH_CLASS_BASE;
    REG_FUNCT7_CLASS[0x01] = REG_ARITH_CLASS_M;
    REG_FUNCT7_CLASS[0x20] = REG_ARITH_CLASS_ALT;

    {
      $alu_reg_arith = ctx_label(ctx);
      // Get <rd> and <funct3>*4
      bus_t _A = ctx_label(ctx);
      bus_t reg_fwd = bus_rel_cb(_A, +3 + 2 + 2 + 2 + 3 + 3);
      ctx_emit_strided_with(ctx, DST_INC | _2DMODE, bus_rel_fld(_A, +1, FLD_SRC, 1), bus_(&inst, 0), 2, 2,
                            _2d_dst_stride(30));
      ctx_emit(ctx, bus(&funct3x4), bus_(TAB_SUPER, SUPER_OP_A_FUNCT3_X4), 1);
      ctx_emit(ctx, bus_rel_fld(reg_fwd, +0, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_A_RD), 1);
      // Get <rs1>*4
      bus_t _B = ctx_label(ctx);
      ctx_emit(ctx, bus_rel_fld(_B, +1, FLD_SRC, 1), bus_(&inst, 1), 2);
      ctx_emit(ctx, bus_rel_fld(reg_fwd, +1, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_B_RS1_X4), 1);
      // Get <rs2>*4, <funct7>, and jump through DISPATCH_X33

      // XXX - REVIEW - XXX - REVIEW - XXX - REVIEW - XXX - REVIEW - XXX - REVIEW - XXX - REVIEW
      //
      // THIS IS BROKEN THIS IS BROKEN THIS IS BROKEN THIS IS BROKEN THIS IS BROKEN THIS IS BROKEN
      // 
      // XXX - REVIEW - XXX - REVIEW - XXX - REVIEW - XXX - REVIEW - XXX - REVIEW - XXX - REVIEW

      // What is this supposed to do?
      // 1. get funct7 and write it to funct7class
      // 2. write (FUNCT7_CLASS[funct7] OR funct3x4) to _C1+0.src
      // 3. load dispatch_reg_arith[...] into <end>.nxt
      // 4. load hyperv_op_c_rs2_x4[inst[2:4]] into the rs2 loader
      
      bus_t _C0 = ctx_label(ctx), ld_nxt = bus_rel_fld(_C0, +2 + 2 + 3, FLD_SRC, 0);
      ctx_emit(ctx, bus_rel_fld(_C0, +1, FLD_SRC, 1), bus_(&inst, 2), 2);
      ctx_emit(ctx, bus(&funct7class), bus_(TAB_SUPER, SUPER_OP_C_FUNCT7), 1);
      bus_t _F = ctx_label(ctx);
      ctx_emit(ctx, bus_rel_fld(_F, +1, FLD_SRC, 0), bus(&funct7class), 1);
      ctx_emit(ctx, ld_nxt, bus(REG_FUNCT7_CLASS), 1);
      stamp_or8(ctx, ld_nxt, ld_nxt, bus(&funct3x4)); // probably 3 blocks
      bus_t _C1 = ctx_label(ctx); // _C0+2+BLKCNT(stamp_or8)
      ctx_emit(ctx, bus_rel_fld(reg_fwd, +2, FLD_NXT, 0), bus(DISPATCH_REG_ARITH), 4); // <ld_nxt>
      ctx_emit(ctx, bus_rel_fld(_C1, +2, FLD_SRC, 1), bus_(&inst, 2), 2);
      ctx_emit(ctx, bus_rel_fld(reg_fwd, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_C_RS2_X4), 1);
      assert(ctx_label(ctx)._0 == reg_fwd._0);
      ctx_emit(ctx, bus_rel_fld($join_reg_arith, +0, FLD_DST, 0), bus(TAB_REGMAP_WR), 1); // <reg_fwd>
      ctx_emit(ctx, bus(rs1), bus(RISCV->REGS), 4);
      ctx_emit(ctx, bus(rs2), bus(RISCV->REGS), 4);
    }

    static volatile _Alignas(256) uint8_t SR_JUMPMAP_TRI[256];
    static volatile _Alignas(256) uint32_t SR_JUMP[3];
    SR_JUMP[0]=$sra._0;
    SR_JUMP[1]=$srl._0;
    SR_JUMP[2]=$illegal._0;
    for(int i=0;i<256;i++) {
      if((i&0xfe)==0x40) SR_JUMPMAP_TRI[i] = 0;
      else if((i&0xfe)==0) SR_JUMPMAP_TRI[i] = 4;
      else SR_JUMPMAP_TRI[i] = 8;
    }
    bus_t $imm_sr = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($imm_sr,+1,FLD_SRC,0), bus_(&inst,3), 1);
    ctx_emit(ctx, bus_rel_fld($imm_sr,+2,FLD_SRC,0), bus(SR_JUMPMAP_TRI), 1);
    ctx_emit(ctx, bus_rel_fld($imm_sr,+3,FLD_NXT,0), bus(SR_JUMP), 4);
    MARK(0);
    
#define dispatch_imm_arith(f3, lbl) dispatch_write(DISPATCH_IMM_ARITH, (f3 << 2), lbl)
    dispatch_imm_arith(0, $add);
    dispatch_imm_arith(4, $xor);
    dispatch_imm_arith(6, $or);
    dispatch_imm_arith(7, $and);
    dispatch_imm_arith(1, $sll);
    dispatch_imm_arith(5, $imm_sr); // exception
    dispatch_imm_arith(2, $slt);
    dispatch_imm_arith(3, $sltu);

    {
      bus_t $imm_dispatch = ctx_label(ctx);
      MARK(0);

      $alu_imm_arith = ctx_label(ctx);
      // Get <rd> and <funct3>*4
      bus_t _A = ctx_label(ctx);
      ctx_emit_strided_with(ctx, DST_INC | _2DMODE, bus_rel_fld(_A, +1, FLD_SRC, 1), bus_(&inst, 0), 2, 2,
                            _2d_dst_stride(30));
      ctx_emit(ctx, bus_rel_fld(_A, +3, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_A_FUNCT3_X4), 1);
      ctx_emit(ctx, bus_rel_fld(_A, +4, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_A_RD), 1);
      ctx_emit(ctx, bus_rel_fld($imm_dispatch, +0, FLD_NXT, 0), bus(DISPATCH_IMM_ARITH), 4);
      ctx_emit(ctx, bus_rel_fld($join_reg_arith, +0, FLD_DST, 0), bus(TAB_REGMAP_WR), 1);

      bus_t _B = ctx_label(ctx);
      ctx_emit(ctx, bus_rel_fld(_B, +1, FLD_SRC, 1), bus_(&inst, 1), 2);
      ctx_emit(ctx, bus_rel_fld(_B, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_B_RS1_X4), 1);
      ctx_emit(ctx, bus(rs1), bus(RISCV->REGS), 4);

      // Get <imm12_sextw> -> rs2
      bus_t _C = ctx_label(ctx);
      ctx_emit(ctx, bus_rel_fld(_C, +1, FLD_SRC, 1), bus_(&inst, 2), 2);
      assert(ctx_at(ctx, bus_rel_cb(_A, +9)));
      ctx_emit(ctx, bus(rs2), bus_(TAB_SUPER, SUPER_OP_IMM12_SEXTW), 4);
      ctx_branch(ctx, $imm_dispatch);
    }
  }

  // --- Second stage decoder: Atomic : Opcode 01011 11
  // funct3/funct5 decode: (funct3=2 all cases)
  {
    bus_t $amo_wb = ctx_label(ctx);
    ctx_emit(ctx, bus(RISCV->REGS), bus(&out), 4);
    ctx_branch(ctx, $all_ret_pcnext);

    static volatile _Alignas(256) uint32_t AMO_RESERVE_SWITCH[2];
    static volatile uint32_t TMP;

    bus_t $lrw = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($lrw,+1,FLD_SRC,0), bus(rs1), 4);
    ctx_emit(ctx, bus(&out), bus_null(), 4);
    ctx_emit(ctx, bus(&RISCV->AMO_RESERVED), bus(rs1), 4);
    ctx_branch(ctx, $amo_wb);
    bus_t $scw_switch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($scw_switch,+1,FLD_NXT,0), bus(AMO_RESERVE_SWITCH), 4);
    MARK(MARK_ATOMIC+41); // dispatch via AMO_RESERVE_SWITCH
    bus_t $scw = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($scw_switch,+0,FLD_SRC,0), bus(&RISCV->AMO_RESERVED), bus(rs1), CMP_X4);
    ctx_branch(ctx, $scw_switch);
    bus_t $scw_ok = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($scw_ok,+1,FLD_DST,0), bus(rs1), 4);
    ctx_emit(ctx, bus_null(), bus(rs2), 4);
    ctx_emit(ctx, bus(&out), bus(&K_ZERO), 4);
    ctx_branch(ctx, $amo_wb);
    bus_t $scw_fail = ctx_label(ctx);
    ctx_emit(ctx, bus(&out), bus(&K_ONE), 4);
    ctx_branch(ctx, $amo_wb);
    AMO_RESERVE_SWITCH[0] = $scw_fail._0;
    AMO_RESERVE_SWITCH[1] = $scw_ok._0;

    bus_t $amoswapw = ctx_label(ctx);
    // rd <- (rs1) | (rs1) <- rs2
    ctx_emit(ctx, bus_rel_fld($amoswapw,+1,FLD_SRC,0), bus(rs1), 4);
    ctx_emit(ctx, bus(&out), bus_null(), 4);
    ctx_emit(ctx, bus_rel_fld($amoswapw,+3,FLD_DST,0), bus(rs1), 4);
    ctx_emit(ctx, bus_null(), bus(rs2), 4);
    ctx_branch(ctx, $amo_wb);
    
    bus_t $amoaddw = ctx_label(ctx);
    {
      bus_t $0 = ctx_label(ctx);
      ctx_emit(ctx, bus_rel_fld($amoaddw,+1,FLD_SRC,0), bus(rs1), 4);
      ctx_emit(ctx, bus(&out), bus_null(), 4);
      stamp_add(ctx, bus(&TMP), bus(rs2), bus(&out));
      bus_t $1 = ctx_label(ctx);
      ctx_emit(ctx, bus_rel_fld($1,+1,FLD_DST,0), bus(rs1), 4);
      ctx_emit(ctx, bus_null(), bus(&TMP), 4);
      ctx_branch(ctx, $amo_wb);
    }

    bus_t $amoandw= ctx_label(ctx);
    {
      bus_t $0 = ctx_label(ctx);
      ctx_emit(ctx, bus_rel_fld($amoandw,+1,FLD_SRC,0), bus(rs1), 4);
      ctx_emit(ctx, bus(&out), bus_null(), 4);
      stamp_and(ctx, bus(&TMP), bus(rs2), bus(&out));
      bus_t $1 = ctx_label(ctx);
      ctx_emit(ctx, bus_rel_fld($1,+1,FLD_DST,0), bus(rs1), 4);
      ctx_emit(ctx, bus_null(), bus(&TMP), 4);
      ctx_branch(ctx, $amo_wb);
    }

    bus_t $amoorw= ctx_label(ctx);
    {
      bus_t $0 = ctx_label(ctx);
      ctx_emit(ctx, bus_rel_fld($amoorw,+1,FLD_SRC,0), bus(rs1), 4);
      ctx_emit(ctx, bus(&out), bus_null(), 4);
      stamp_or(ctx, bus(&TMP), bus(rs2), bus(&out));
      bus_t $1 = ctx_label(ctx);
      ctx_emit(ctx, bus_rel_fld($1,+1,FLD_DST,0), bus(rs1), 4);
      ctx_emit(ctx, bus_null(), bus(&TMP), 4);
      ctx_branch(ctx, $amo_wb);
    }

    bus_t $amoxorw= ctx_label(ctx);
    {
      bus_t $0 = ctx_label(ctx);
      ctx_emit(ctx, bus_rel_fld($amoxorw,+1,FLD_SRC,0), bus(rs1), 4);
      ctx_emit(ctx, bus(&out), bus_null(), 4);
      stamp_xor(ctx, bus(&TMP), bus(rs2), bus(&out));
      bus_t $1 = ctx_label(ctx);
      ctx_emit(ctx, bus_rel_fld($1,+1,FLD_DST,0), bus(rs1), 4);
      ctx_emit(ctx, bus_null(), bus(&TMP), 4);
      ctx_branch(ctx, $amo_wb);
    }
    
    // rd <- (rs1); (rs1) <- min/max (rs1), rs2

    bus_t $amominmaxw_ld=ctx_label(ctx);
    ctx_emit(ctx, bus_null(), bus(RS), 4);
    ctx_branch(ctx, $amo_wb);

    bus_t $amomaxw= ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($amomaxw,+2,FLD_SRC,0), bus(rs1), 4);
    ctx_emit(ctx, bus_rel_fld($amominmaxw_ld,+0,FLD_DST,0), bus(rs1), 4);
    ctx_emit(ctx, bus(rs1), bus_null(), 4);
    ctx_emit(ctx, bus(&out), bus(rs1), 4);
    stamp_ge_bww(ctx, bus_rel_fld($amominmaxw_ld,+0,FLD_SRC,0), bus(rs1), bus(rs2), CMP_X4|CMP_INV);
    ctx_branch(ctx, $amominmaxw_ld);

    bus_t $amominw= ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($amominw,+2,FLD_SRC,0), bus(rs1), 4);
    ctx_emit(ctx, bus_rel_fld($amominmaxw_ld,+0,FLD_DST,0), bus(rs1), 4);
    ctx_emit(ctx, bus(rs1), bus_null(), 4);
    ctx_emit(ctx, bus(&out), bus(rs1), 4);
    stamp_ge_bww(ctx, bus_rel_fld($amominmaxw_ld,+0,FLD_SRC,0), bus(rs1), bus(rs2), CMP_X4);
    ctx_branch(ctx, $amominmaxw_ld);

    bus_t $amomaxuw= ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($amomaxuw,+2,FLD_SRC,0), bus(rs1), 4);
    ctx_emit(ctx, bus_rel_fld($amominmaxw_ld,+0,FLD_DST,0), bus(rs1), 4);
    ctx_emit(ctx, bus(rs1), bus_null(), 4);
    ctx_emit(ctx, bus(&out), bus(rs1), 4);
    stamp_geu_bww(ctx, bus_rel_fld($amominmaxw_ld,+0,FLD_SRC,0), bus(rs1), bus(rs2), CMP_X4|CMP_INV);
    ctx_branch(ctx, $amominmaxw_ld);

    bus_t $amominuw= ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($amominuw,+2,FLD_SRC,0), bus(rs1), 4);
    ctx_emit(ctx, bus_rel_fld($amominmaxw_ld,+0,FLD_DST,0), bus(rs1), 4);
    ctx_emit(ctx, bus(rs1), bus_null(), 4);
    ctx_emit(ctx, bus(&out), bus(rs1), 4);
    stamp_geu_bww(ctx, bus_rel_fld($amominmaxw_ld,+0,FLD_SRC,0), bus(rs1), bus(rs2), CMP_X4);
    ctx_branch(ctx, $amominmaxw_ld);
    
    static volatile _Alignas(256) uint32_t DISPATCH_ATOMIC[64];
    for(int i=0;i<64;i++) DISPATCH_ATOMIC[i]=$illegal._0;
    DISPATCH_ATOMIC[0x00] = $amoaddw._0;
    DISPATCH_ATOMIC[0x01] = $amoswapw._0;
    DISPATCH_ATOMIC[0x02] = $lrw._0;
    DISPATCH_ATOMIC[0x03] = $scw._0;
    DISPATCH_ATOMIC[0x04] = $amoxorw._0;
    DISPATCH_ATOMIC[0x08] = $amoorw._0;
    DISPATCH_ATOMIC[0x0C] = $amoandw._0;
    DISPATCH_ATOMIC[0x10] = $amominw._0;
    DISPATCH_ATOMIC[0x14] = $amomaxw._0;
    DISPATCH_ATOMIC[0x18] = $amominuw._0;
    DISPATCH_ATOMIC[0x1C] = $amomaxuw._0;
    static volatile _Alignas(256) uint8_t FUNCT5[256];
    for(int i=0;i<256;i++) FUNCT5[i] = (i & 0xf8) >> 1;

    bus_t $dispatch_jump = ctx_label(ctx);
    MARK(MARK_ATOMIC+11);
    
    $alu_atomic = ctx_label(ctx);
    bus_t $A = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($A, +1, FLD_SRC, 1), bus_(&inst, 0), 2);
    ctx_emit(ctx, bus_rel_fld($A, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_A_RD), 1);
    ctx_emit(ctx, bus_rel_fld($amo_wb, +0, FLD_DST, 0), bus(TAB_REGMAP_WR), 1);
    bus_t $B = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($B, +1, FLD_SRC, 1), bus_(&inst, 1), 2);
    ctx_emit(ctx, bus_rel_fld($B, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_B_RS1_X4), 1);
    ctx_emit(ctx, bus(rs1), bus(RISCV->REGS), 4);
    bus_t $C = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($C, +1, FLD_SRC, 1), bus_(&inst, 2), 2);
    ctx_emit(ctx, bus_rel_fld($C, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_C_RS2_X4), 1);
    ctx_emit(ctx, bus(rs2), bus(RISCV->REGS), 4);
    bus_t $dispatch_load = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($dispatch_load,+1,FLD_SRC,0), bus_(&inst,3), 1);
    ctx_emit(ctx, bus_rel_fld($dispatch_load,+2,FLD_SRC,0), bus(FUNCT5), 1);
    ctx_emit(ctx, bus_rel_fld($dispatch_jump,+0,FLD_NXT,0), bus(DISPATCH_ATOMIC), 4);
    ctx_branch(ctx, $dispatch_jump);
  }

  {
    // Env: I-type. imm12[11:0] | rs1 | funct3 | rd | opcode
    // ecall: funct3=0 imm=0
    // ebreak: funct3=0 imm=1

    static volatile _Alignas(256) uint32_t DISPATCH_ENV[64];
    for (int i = 0; i < 64; i++)
      DISPATCH_ENV[i] = $illegal._0;

    static volatile _Alignas(256) uint32_t ENV0_MRET_SWITCH[2];
    static volatile _Alignas(256) uint32_t ENV0_WFI_SWITCH[2];
    static volatile _Alignas(256) uint32_t ENV0_EBREAK_SWITCH[2];
    static volatile _Alignas(256) uint32_t ENV0_ECALL_SWITCH[2];
    static volatile uint32_t csr_src, csr_dst, csr_num, csr_old, csr_tmp, csr_zimm;
    static volatile uint8_t csr_rd;
    static volatile uint32_t K_ECALL_CSR = 0;
    static volatile uint32_t K_EBREAK_CSR = 1;
    static volatile uint32_t K_WFI_CSR = 0x105;
    static volatile uint32_t K_MRET_CSR = 0x302;
    static volatile uint32_t K_MSTATUS_MRET_CLEAR = ~0x1888u;
    static volatile uint32_t K_MSTATUS_MPIE = 1 << 7;
    static volatile uint32_t K_MSTATUS_MPIE_SHIFT = 4;
    static volatile uint32_t K_MSTATUS_MPP = 0x1800;
    static volatile uint32_t K_MSTATUS_MPP_SHIFT = 11;
    static volatile uint32_t mret_tmp, mret_mie, mret_priv;

    bus_t $csr_wb = ctx_label(ctx);
    ctx_emit(ctx, bus(RISCV->REGS), bus(&csr_old), 4);
    ctx_branch(ctx, $all_ret_pcnext);

    bus_t $csr_store = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($csr_store, +1, FLD_DST, 0), bus(&csr_dst), 4);
    ctx_emit(ctx, bus_null(), bus(&csr_tmp), 4);
    ctx_branch(ctx, $csr_wb);

    bus_t $csrrw = ctx_label(ctx);
    ctx_emit(ctx, bus(&csr_tmp), bus(rs1), 4);
    ctx_branch(ctx, $csr_store);

    bus_t $csrrs = ctx_label(ctx);
    stamp_or(ctx, bus(&csr_tmp), bus(&csr_old), bus(rs1));
    ctx_branch(ctx, $csr_store);

    bus_t $csrrc = ctx_label(ctx);
    stamp_not(ctx, bus(&csr_tmp), bus(rs1));
    stamp_and(ctx, bus(&csr_tmp), bus(&csr_old), bus(&csr_tmp));
    ctx_branch(ctx, $csr_store);

    bus_t $csrrwi = ctx_label(ctx);
    ctx_emit(ctx, bus(&csr_tmp), bus(&csr_zimm), 4);
    ctx_branch(ctx, $csr_store);

    bus_t $csrrsi = ctx_label(ctx);
    stamp_or(ctx, bus(&csr_tmp), bus(&csr_old), bus(&csr_zimm));
    ctx_branch(ctx, $csr_store);

    bus_t $csrrci = ctx_label(ctx);
    stamp_not(ctx, bus(&csr_tmp), bus(&csr_zimm));
    stamp_and(ctx, bus(&csr_tmp), bus(&csr_old), bus(&csr_tmp));
    ctx_branch(ctx, $csr_store);

    DISPATCH_ENV[1] = $csrrw._0;
    DISPATCH_ENV[2] = $csrrs._0;
    DISPATCH_ENV[3] = $csrrc._0;
    DISPATCH_ENV[5] = $csrrwi._0;
    DISPATCH_ENV[6] = $csrrsi._0;
    DISPATCH_ENV[7] = $csrrci._0;

    bus_t $mret = ctx_label(ctx);
    stamp_and(ctx, bus(&mret_priv), bus(&RISCV->MSTATUS), bus(&K_MSTATUS_MPP));
    stamp_srl(ctx, bus(&mret_priv), bus(&mret_priv), bus(&K_MSTATUS_MPP_SHIFT));
    stamp_and(ctx, bus(&mret_tmp), bus(&RISCV->MSTATUS), bus(&K_MSTATUS_MPIE));
    stamp_srl(ctx, bus(&mret_mie), bus(&mret_tmp), bus(&K_MSTATUS_MPIE_SHIFT));
    stamp_and(ctx, bus(&mret_tmp), bus(&RISCV->MSTATUS), bus(&K_MSTATUS_MRET_CLEAR));
    stamp_or(ctx, bus(&mret_tmp), bus(&mret_tmp), bus(&K_MSTATUS_MPIE));
    stamp_or(ctx, bus(&mret_tmp), bus(&mret_tmp), bus(&mret_mie));
    ctx_emit(ctx, bus(&RISCV->MSTATUS), bus(&mret_tmp), 4);
    ctx_emit(ctx, bus(&RISCV->PRIV), bus(&mret_priv), 4);
    ctx_emit(ctx, bus(&RISCV->PC), bus(&RISCV->MEPC), 4);
    ctx_branch(ctx, $all_ret_direct);

    bus_t $wfi = ctx_label(ctx);
    ctx_emit(ctx, bus(&RISCV->WFI), bus(&K_ONE), 4);
    ctx_branch(ctx, $all_ret_pcnext);

    bus_t $env0_mret_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($env0_mret_dispatch, +1, FLD_NXT, 0), bus(ENV0_MRET_SWITCH), 4);
    MARK(MARK_ENV + 2);
    bus_t $env0_wfi_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($env0_wfi_dispatch, +1, FLD_NXT, 0), bus(ENV0_WFI_SWITCH), 4);
    MARK(MARK_ENV + 3);
    bus_t $env0_ebreak_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($env0_ebreak_dispatch, +1, FLD_NXT, 0), bus(ENV0_EBREAK_SWITCH), 4);
    MARK(MARK_ENV + 4);
    bus_t $env0_ecall_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($env0_ecall_dispatch, +1, FLD_NXT, 0), bus(ENV0_ECALL_SWITCH), 4);
    MARK(MARK_ENV + 5);

    bus_t $env0 = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($env0_mret_dispatch, +0, FLD_SRC, 0), bus(&csr_num), bus(&K_MRET_CSR),
                 CMP_X4);
    ctx_branch(ctx, $env0_mret_dispatch);

    bus_t $env0_wfi_check = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($env0_wfi_dispatch, +0, FLD_SRC, 0), bus(&csr_num), bus(&K_WFI_CSR),
                 CMP_X4);
    ctx_branch(ctx, $env0_wfi_dispatch);

    bus_t $env0_ebreak_check = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($env0_ebreak_dispatch, +0, FLD_SRC, 0), bus(&csr_num),
                 bus(&K_EBREAK_CSR), CMP_X4);
    ctx_branch(ctx, $env0_ebreak_dispatch);

    bus_t $env0_ecall_check = ctx_label(ctx);
    stamp_eq_bww(ctx, bus_rel_fld($env0_ecall_dispatch, +0, FLD_SRC, 0), bus(&csr_num),
                 bus(&K_ECALL_CSR), CMP_X4);
    ctx_branch(ctx, $env0_ecall_dispatch);

    ENV0_MRET_SWITCH[0] = $env0_wfi_check._0;
    ENV0_MRET_SWITCH[1] = $mret._0;
    ENV0_WFI_SWITCH[0] = $env0_ebreak_check._0;
    ENV0_WFI_SWITCH[1] = $wfi._0;
    ENV0_EBREAK_SWITCH[0] = $env0_ecall_check._0;
    ENV0_EBREAK_SWITCH[1] = $ebreak._0;
    ENV0_ECALL_SWITCH[0] = $illegal._0;
    ENV0_ECALL_SWITCH[1] = $ecall_m._0;
    DISPATCH_ENV[0] = $env0._0;

    bus_t $csr_dispatch = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($csr_dispatch, +1, FLD_NXT, 0), bus(DISPATCH_ENV), 4);
    MARK(MARK_ENV + 1);

    bus_t $csr_read = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($csr_read, +1, FLD_SRC, 0), bus(&csr_src), 4);
    ctx_emit(ctx, bus(&csr_old), bus_null(), 4);
    ctx_branch(ctx, $csr_dispatch);

    $alu_env = ctx_label(ctx);
    // Get <rd>, <funct3>, <rs1>, <zimm>, and <csr>.
    bus_t _A = ctx_label(ctx);
    ctx_emit_strided_with(ctx, DST_INC | _2DMODE, bus_rel_fld(_A, +1, FLD_SRC, 1), bus_(&inst, 0), 2,
                          2, _2d_dst_stride(30));
    ctx_emit(ctx, bus(&funct3x4), bus_(TAB_SUPER, SUPER_OP_A_FUNCT3_X4), 1);
    ctx_emit(ctx, bus(&csr_rd), bus_(TAB_SUPER, SUPER_OP_A_RD), 1);
    ctx_emit(ctx, bus_rel_fld($csr_dispatch, +0, FLD_SRC, 0), bus(&funct3x4), 1);
    ctx_emit(ctx, bus_rel_fld(_A, +5, FLD_SRC, 0), bus(&csr_rd), 1);
    ctx_emit(ctx, bus_rel_fld($csr_wb, +0, FLD_DST, 0), bus(TAB_REGMAP_WR), 1);

    bus_t _B = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld(_B, +1, FLD_SRC, 1), bus_(&inst, 1), 2);
    ctx_emit(ctx, bus_rel_fld(_B, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_B_RS1_X4), 1);
    ctx_emit(ctx, bus(rs1), bus(RISCV->REGS), 4);
    ctx_emit(ctx, bus(&csr_zimm), bus(&K_ZERO), 4);
    bus_t _BZ = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld(_BZ, +1, FLD_SRC, 1), bus_(&inst, 1), 2);
    ctx_emit(ctx, bus_(&csr_zimm, 0), bus_(TAB_SUPER, SUPER_OP_B_ZIMM), 1);

    bus_t _C = ctx_label(ctx);
    ctx_emit_strided_with(ctx, DST_INC | _2DMODE, bus_rel_fld(_C, +1, FLD_SRC, 1), bus_(&inst, 2), 3,
                          2, _2d_dst_stride(30));
    ctx_emit(ctx, bus(&csr_src), bus_(TAB_SUPER, SUPER_OP_C_CSR_SRC), 4);
    ctx_emit(ctx, bus(&csr_dst), bus_(TAB_SUPER, SUPER_OP_C_CSR_DST), 4);
    ctx_emit(ctx, bus(&csr_num), bus_(TAB_SUPER, SUPER_OP_C_CSR32), 4);
    ctx_branch(ctx, $csr_read);
  }
  
  {
    static volatile uint32_t custom_dst, custom_src, custom_len, custom_len_nonzero;
    static volatile _Alignas(16) uint32_t custom_src_pattern[4];
    static volatile _Alignas(256) uint32_t CUSTOM_MEMCPY_LEN_SWITCH[2];
    static volatile _Alignas(256) uint32_t CUSTOM_MEMSET_LEN_SWITCH[2];

    $emu_custom_2 = ctx_label(ctx);
    bus_t $memcpy_rd = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($memcpy_rd, +1, FLD_SRC, 1), bus_(&inst, 0), 2);
    ctx_emit(ctx, bus_rel_fld($memcpy_rd, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_A_RD_X4), 1);
    ctx_emit(ctx, bus(&custom_dst), bus(RISCV->REGS), 4);

    bus_t $memcpy_rs1 = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($memcpy_rs1, +1, FLD_SRC, 1), bus_(&inst, 1), 2);
    ctx_emit(ctx, bus_rel_fld($memcpy_rs1, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_B_RS1_X4), 1);
    ctx_emit(ctx, bus(&custom_src), bus(RISCV->REGS), 4);

    bus_t $memcpy_rs2 = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($memcpy_rs2, +1, FLD_SRC, 1), bus_(&inst, 2), 2);
    ctx_emit(ctx, bus_rel_fld($memcpy_rs2, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_C_RS2_X4), 1);
    ctx_emit(ctx, bus(&custom_len), bus(RISCV->REGS), 4);

    stamp_eq_bww(ctx, bus(&custom_len_nonzero), bus(&custom_len), bus(&K_ZERO), CMP_X4 | CMP_INV);
    bus_t $memcpy_len_dispatch = bus_rel_cb(ctx_label(ctx), +1);
    ctx_emit(ctx, bus_rel_fld($memcpy_len_dispatch, +0, FLD_SRC, 0), bus(&custom_len_nonzero), 1);
    ctx_emit(ctx, bus_rel_fld($memcpy_len_dispatch, +1, FLD_NXT, 0), bus(CUSTOM_MEMCPY_LEN_SWITCH), 4);
    MARK(MARK_CUSTOM + 1);

    bus_t $memcpy_patch = ctx_label(ctx);
    bus_t $memcpy_xfer = bus_rel_cb($memcpy_patch, +3);
    ctx_emit(ctx, bus_rel_fld($memcpy_xfer, +0, FLD_DST, 0), bus(&custom_dst), 4);
    ctx_emit(ctx, bus_rel_fld($memcpy_xfer, +0, FLD_SRC, 0), bus(&custom_src), 4);
    ctx_emit(ctx, bus_rel_fld($memcpy_xfer, +0, FLD_LEN, 0), bus(&custom_len), 4);
    assert(ctx_at(ctx, $memcpy_xfer));
    ctx_emit_with(ctx, DST_INC | SRC_INC, bus_null(), bus_null(), 0);
    ctx_branch(ctx, $all_ret_pcnext);
    CUSTOM_MEMCPY_LEN_SWITCH[0] = $all_ret_pcnext._0;
    CUSTOM_MEMCPY_LEN_SWITCH[1] = $memcpy_patch._0;

    $emu_custom_3 = ctx_label(ctx);
    bus_t $memset_rd = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($memset_rd, +1, FLD_SRC, 1), bus_(&inst, 0), 2);
    ctx_emit(ctx, bus_rel_fld($memset_rd, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_A_RD_X4), 1);
    ctx_emit(ctx, bus(&custom_dst), bus(RISCV->REGS), 4);

    bus_t $memset_rs1 = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($memset_rs1, +1, FLD_SRC, 1), bus_(&inst, 1), 2);
    ctx_emit(ctx, bus_rel_fld($memset_rs1, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_B_RS1_X4), 1);
    ctx_emit(ctx, bus(&custom_src), bus(RISCV->REGS), 4);
    ctx_emit_strided_with(ctx, DST_INC | _2DMODE, bus(&custom_src_pattern[0]), bus(&custom_src), 16, 1, 0);

    bus_t $memset_rs2 = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($memset_rs2, +1, FLD_SRC, 1), bus_(&inst, 2), 2);
    ctx_emit(ctx, bus_rel_fld($memset_rs2, +2, FLD_SRC, 0), bus_(TAB_SUPER, SUPER_OP_C_RS2_X4), 1);
    ctx_emit(ctx, bus(&custom_len), bus(RISCV->REGS), 4);

    stamp_eq_bww(ctx, bus(&custom_len_nonzero), bus(&custom_len), bus(&K_ZERO), CMP_X4 | CMP_INV);
    bus_t $memset_len_dispatch = bus_rel_cb(ctx_label(ctx), +1);
    ctx_emit(ctx, bus_rel_fld($memset_len_dispatch, +0, FLD_SRC, 0), bus(&custom_len_nonzero), 1);
    ctx_emit(ctx, bus_rel_fld($memset_len_dispatch, +1, FLD_NXT, 0), bus(CUSTOM_MEMSET_LEN_SWITCH), 4);
    MARK(MARK_CUSTOM + 2);

    bus_t $memset_patch = ctx_label(ctx);
    bus_t $memset_xfer = bus_rel_cb($memset_patch, +2);
    ctx_emit(ctx, bus_rel_fld($memset_xfer, +0, FLD_DST, 0), bus(&custom_dst), 4);
    ctx_emit(ctx, bus_rel_fld($memset_xfer, +0, FLD_LEN, 0), bus(&custom_len), 4);
    assert(ctx_at(ctx, $memset_xfer));
    ctx_emit_with(ctx, DST_INC, bus_null(), bus(&custom_src_pattern[0]), 0);
    ctx_branch(ctx, $all_ret_pcnext);
    CUSTOM_MEMSET_LEN_SWITCH[0] = $all_ret_pcnext._0;
    CUSTOM_MEMSET_LEN_SWITCH[1] = $memset_patch._0;
  }

  {
    // Halt emulation (PASS)
    $emu_custom_0 = ctx_label(ctx);
    static volatile uint32_t K_STATE_PASS = EMU_STATE_PASS;
    ctx_emit(ctx, bus(&RISCV->EMU_STATE), bus(&K_STATE_PASS), 4);
    ctx_end(ctx);
    
    // Halt emulation (FAIL)
    $emu_custom_1 = ctx_label(ctx);
    static volatile uint32_t K_STATE_FAIL = EMU_STATE_FAIL;
    ctx_emit(ctx, bus(&RISCV->EMU_STATE), bus(&K_STATE_FAIL), 4);
    ctx_end(ctx);
  }

  // --- First stage decoder: Populate
  for (int i = 0; i < 64; i++)
    DISPATCH_STAGE_1[i] = $illegal._0;

  DISPATCH_STAGE_1[0b01101] = $alu_lui._0;
  DISPATCH_STAGE_1[0b00101] = $alu_auipc._0;
  DISPATCH_STAGE_1[0b11011] = $alu_jal._0;
  DISPATCH_STAGE_1[0b11001] = $alu_jalr._0;
  DISPATCH_STAGE_1[0b11000] = $alu_bcc._0;
  DISPATCH_STAGE_1[0b00000] = $alu_load._0;
  DISPATCH_STAGE_1[0b01000] = $alu_store._0;
  DISPATCH_STAGE_1[0b00100] = $alu_imm_arith._0;
  DISPATCH_STAGE_1[0b01100] = $alu_reg_arith._0;
  DISPATCH_STAGE_1[0b00011] = $alu_fence._0;
  DISPATCH_STAGE_1[0b11100] = $alu_env._0;
  DISPATCH_STAGE_1[0b01011] = $alu_atomic._0;

  DISPATCH_STAGE_1[0b00010] = $emu_custom_0._0;
  DISPATCH_STAGE_1[0b01010] = $emu_custom_1._0;
  DISPATCH_STAGE_1[0b10110] = $emu_custom_2._0;
  DISPATCH_STAGE_1[0b11110] = $emu_custom_3._0;

  // Having one op5 that's definitely illegal allows us to intercept instructions with illegal lo2
  // at the OP5_MASK level
  assert(DISPATCH_STAGE_1[0b11111] == $illegal._0);
}
