#include <generic/printf.h>
#include <stdint.h>

#include "arith.h"
#include "bcm2835/dma/private.h"
#include "rv_emu.h"

volatile RVState RV[1];

static volatile uint32_t CSR_SINK;
static volatile uint32_t CSR_MISA = 0x40001101; // RV32IMA + Zicsr/Zifencei-ish enough for probes.
static volatile uint32_t CSR_MHARTID = 0;

// static volatile uint32_t MMIO_PLIC_UART_PRIORITY = 0x0c000028;
// static volatile uint32_t MMIO_PLIC_PENDING = 0x0c001000;
// static volatile uint32_t MMIO_PLIC_ENABLE = 0x0c002000;
// static volatile uint32_t MMIO_PLIC_THRESHOLD = 0x0c200000;
// static volatile uint32_t MMIO_PLIC_CLAIM = 0x0c200004;
// static volatile uint32_t MMIO_CLINT_MTIMECMP_LO = 0x02004000;
// static volatile uint32_t MMIO_CLINT_MTIMECMP_HI = 0x02004004;
// static volatile uint32_t MMIO_CLINT_MTIME_LO = 0x0200bff8;
// static volatile uint32_t MMIO_CLINT_MTIME_HI = 0x0200bffc;
// static volatile uint32_t MMIO_UART_THR_RBR = 0x10000000;
// static volatile uint32_t MMIO_UART_IER = 0x10000004;
// static volatile uint32_t MMIO_UART_IIR_FCR = 0x10000008;
// static volatile uint32_t MMIO_UART_LCR = 0x1000000c;
// static volatile uint32_t MMIO_UART_MCR = 0x10000010;
// static volatile uint32_t MMIO_UART_LSR = 0x10000014;
// static volatile uint32_t MMIO_UART_MSR = 0x10000018;
// static volatile uint32_t MMIO_UART_SCR = 0x1000001c;
// static volatile uint32_t MMIO_PLIC_PRIORITY_BASE = 0x0c000000;
// static volatile uint32_t MMIO_PLIC_PRIORITY_END = 0x0c00002c;

static volatile uint32_t *
csr_read_ptr(uint32_t csr)
{
  switch (csr) {
    case 0x300:
      return &RV->MSTATUS;
    case 0x301:
      return &CSR_MISA;
    case 0x304:
      return &RV->MIE;
    case 0x305:
      return &RV->MTVEC;
    case 0x340:
      return &RV->MSCRATCH;
    case 0x341:
      return &RV->MEPC;
    case 0x342:
      return &RV->MCAUSE;
    case 0x343:
      return &RV->MTVAL;
    case 0x344:
      return &RV->MIP;
    case 0xb00:
      return &RV->CYCLEL;
    case 0xb80:
      return &RV->CYCLEH;
    case 0xc00:
      return &RV->CYCLEL;
    case 0xc01:
      return &RV->MTIMECMPLO;
    case 0xc80:
      return &RV->CYCLEH;
    case 0xc81:
      return &RV->MTIMECMPHI;
    case 0xf14:
      return &CSR_MHARTID;
    default:
      return (uint32_t *)ZERO;
  }
}

static volatile uint32_t *
csr_write_ptr(uint32_t csr)
{
  switch (csr) {
    case 0x300:
      return &RV->MSTATUS;
    case 0x304:
      return &RV->MIE;
    case 0x305:
      return &RV->MTVEC;
    case 0x340:
      return &RV->MSCRATCH;
    case 0x341:
      return &RV->MEPC;
    case 0x342:
      return &RV->MCAUSE;
    case 0x343:
      return &RV->MTVAL;
    case 0x344:
      return &RV->MIP;
    default:
      return &CSR_SINK;
  }
}

INITIALIZER(rv_regmaps)
{
  SUPER[0 << 8 | SUPERV_RegisterMapRead] = 0;
  SUPER[0 << 8 | SUPERV_RegisterMapWrite] = 128;
  for (size_t i = 1; i < 32; i++) {
    // SUPER[i] = 4 * i;
    SUPER[i << 8 | SUPERV_RegisterMapRead] = 4 * i;
    SUPER[i << 8 | SUPERV_RegisterMapWrite] = 4 * i;
  }
}

INITIALIZER(rv_decode)
{
  // <rd> and <funct3> are pulled from a 15:0 view of the opcode
  for (size_t k = 0; k < (1 << 16); k++) {
    // <rd> is 11:7
    HYPER[(k << 8) | HYPERV_OP_A_RD] = (k >> 7) & 0x1f;
    // For custom R-type operands: rd is read as a source, so x0 maps to REGS[0].
    HYPER[(k << 8) | HYPERV_OP_A_RD_X4] = (k >> 5) & 0x7c;
    // <funct3> is 14:12
    HYPER[(k << 8) | HYPERV_OP_A_FUNCT3_X4] = (k >> 10) & 0x1c;
  }

  // <rs1> is pulled from a 23:8 view of the opcode
  for (size_t k = 0; k < (1 << 16); k++) {
    // <rs1> is 19:15; based against :8, that's 11:7
    HYPER[(k << 8) | HYPERV_OP_B_RS1_X4] = (k >> 5) & 0x7c;
    HYPER[(k << 8) | HYPERV_OP_B_ZIMM] = (k >> 7) & 0x1f;
  }

  // <rs2> and <funct7> are pulled from a 31:16 view of the opcode
  for (size_t k = 0; k < (1 << 16); k++) {
    // <rs2> is 24:20; based against :16, that's 8:4
    HYPER[(k << 8) | HYPERV_OP_C_RS2_X4] = (k >> 2) & 0x7c;
    // <funct7> is 31:25; based against :16, that's 15:9
    HYPER[(k << 8) | HYPERV_OP_C_FUNCT7] = (k >> 9) & 0x7f;
  }

  // JAL decoder. Threaded through the empty portions of HYPER.
  for (size_t k = 0; k < (1 << 20); k++) {
    // 20 | 10:1 | 11 | 19:12
    // 1     10    1     8
    // 19    9     8     0 in the instruction fragment; imm bit 20 is the sign bit.
    size_t p = (k & 0x000ff) << 12 | (k & 0x00100) << 3 | (k & 0x7fe00) >> 8 |
               ((k & 0x80000) ? 0xfff00000 : 0);
    __builtin_memcpy((void *)&HYPER[(k << 4)], &p, 4);
  }

  for (size_t k = 0; k < (1 << 16); k++) {
    uint16_t bits = k >> 4;
    if (k & 0x8000)
      bits |= 0xf000;
    HYPER[k << 8 | HYPERV_OP_IMM12_SEXTW | 0] = bits & 0xff;
    HYPER[k << 8 | HYPERV_OP_IMM12_SEXTW | 1] = bits >> 8;
    HYPER[k << 8 | HYPERV_OP_IMM12_SEXTW | 2] = (bits & 0x8000) ? 0xff : 0x00;
    HYPER[k << 8 | HYPERV_OP_IMM12_SEXTW | 3] = (bits & 0x8000) ? 0xff : 0x00;
  }

  for (size_t k = 0; k < (1 << 16); k++) {
    uint32_t csr = k >> 4;
    uint32_t src = bus(csr_read_ptr(csr))._0;
    uint32_t dst = bus(csr_write_ptr(csr))._0;
    __builtin_memcpy((void *)&HYPER[(k << 8) + HYPERV_OP_C_CSR_SRC], &src, 4);
    __builtin_memcpy((void *)&HYPER[(k << 8) + HYPERV_OP_C_CSR_DST], &dst, 4);
    __builtin_memcpy((void *)&HYPER[(k << 8) + HYPERV_OP_C_CSR32], &csr, 4);
  }

  for (size_t i = 0; i < 256; i++)
    SUPER[i << 8 | SUPERV_OP5_MASK] = (i & 3) == 3 ? i & 0x7c : 0b111'1110;

  for (size_t i = 0; i < 256; i++)
    SUPER[i << 8 | SUPERV_Amo_Funct5x4] = (i & 0xf8) >> 1;

  for (size_t i = 0; i < 256; i++)
    SUPER[i << 8 | SUPERV_IsMmioAddrFromHiByteX4] = (i == 0) ? 4 : 0;

  for (int i = 0; i < (1 << 16); i++) {
    uint16_t imm12 = i >> 4;
    if (imm12 == 0)
      HYPER[i << 8 | HYPERV_OP_C_ENV0] = 0x4;
    else if (imm12 == 1)
      HYPER[i << 8 | HYPERV_OP_C_ENV0] = 0x8;
    else if (imm12 == 0x302)
      HYPER[i << 8 | HYPERV_OP_C_ENV0] = 0xc;
    else if (imm12 == 0x105)
      HYPER[i << 8 | HYPERV_OP_C_ENV0] = 0x10;
    else if ((imm12 >> 6) == 0x2f)
      HYPER[i << 8 | HYPERV_OP_C_ENV0] = 0x14;
    else
      HYPER[i << 8 | HYPERV_OP_C_ENV0] = 0x0;
  }
}

INITIALIZER(rv_custom_ops)
{
  for (size_t i = 0; i < 256; i++)
    SUPER[i << 8 | SUPERV_HandleMPIEOnTrap] = (i & 0x77) | ((i & (1 << 3)) << 4);
  for (size_t i = 0;i < 256; i++)
    SUPER[i << 8 | SUPERV_HandleMPIEOnMRET] = (i & 0x77) | ((i & (1 << 7)) >> 4) | (1 << 7);
}

INITIALIZER(bcc_parts)
{
  for (int i = 0; i < 256; i++) {
    uint16_t b12 = (i & 0x80) >> 7;
    uint16_t b10_5 = (i & 0x7e) >> 1;
    uint16_t bhi = (b12 << 12) | (b10_5 << 5) | (b12 ? 0xf000 : 0);
    SUPER[(i << 8) | SUPERV_BccRead_12__10_5 | 0] = bhi & 0xff;
    SUPER[(i << 8) | SUPERV_BccRead_12__10_5 | 1] = bhi >> 8;
    SUPER[(i << 8) | SUPERV_BccRead_12__10_5 | 2] = b12 ? 0xff : 0;
    SUPER[(i << 8) | SUPERV_BccRead_12__10_5 | 3] = b12 ? 0xff : 0;

    uint16_t b4_1 = (i & 0x1e) >> 1;
    uint16_t b11 = i & 1;
    uint16_t blo = (b11 << 11) | (b4_1 << 1);
    SUPER[i << 8 | SUPERV_BccRead_11__4_1 | 0] = blo & 0xff;
    SUPER[i << 8 | SUPERV_BccRead_11__4_1 | 1] = blo >> 8;
  }
}

// static inline void
#define set_w(table, word_index, label) table[word_index] = label._0

volatile uint32_t mark = 0;
// #define MARK_(C, K)                        \
//   do {                                     \
//     static volatile uint32_t C = K;        \
//     ctx_emit(ctx, bus(&mark), bus(&C), 4); \
//   } while (0)
// #define MARK(K) MARK_(CONCAT(_MARK_, __COUNTER__), K)

struct
{
  alignas(256) uint32_t RS[2];
  uint8_t funct7cls;
  uint32_t out;
  uint32_t inst;
} volatile RV_Decoder;

#define MARK(_mark)                         \
  do {                                      \
    static volatile uint32_t _MARK = _mark; \
    Emit(ctx, bus(&mark), bus(&_MARK), 4);  \
  } while (0)

uint32_t K_EMU_STATE_RUNNING = 0;
uint32_t K_EMU_STATE_PASS = 1;
uint32_t K_EMU_STATE_FAIL = 2;
uint32_t K_EMU_STATE_DEBUG_TIMEOUT = 3;
uint32_t K_EMU_STATE_DEBUG_TRAP = 4;

busad
Const32(uint32_t x)
{
  volatile uint32_t *alloc = MonomorphizeOn(x, arena_alloc(&ARENA, sizeof x, sizeof x));
  *alloc = x;
  return bus(alloc);
}

busad
ConstAd(volatile void *p)
{
  return Const32(bus(p)._0);
}

busad
Const8(uint8_t x)
{
  volatile uint8_t *alloc = MonomorphizeOn(x, arena_alloc(&ARENA, sizeof x, sizeof x));
  *alloc = x;
  return bus(alloc);
}

static void
RVTrapEntry(emit_ctx *ctx)
{
  // TODO: Is this actually the correct behavior? (MIE=0)

  enum
  {
    MSTATUS_BYTE1_MPP_MASK = 3 << 3,
    RV_PRIV_M = 3,
  };
  busad $trap_save_priv = Label(ctx);
  Emit(ctx, bus_rel_fld($trap_save_priv, +1, FLD_SRC, 2), bus_(&RV->MSTATUS, 1), 1);
  Emit(ctx,
       bus_rel_fld($trap_save_priv, +4, FLD_SRC, 2),
       bus_(HYPER, MSTATUS_BYTE1_MPP_MASK << 8 | HYPER_BIC),
       1);
  Emit(ctx, bus_rel_fld($trap_save_priv, +3, FLD_SRC,2), bus(&RV->PRIV), 1);
  Emit(ctx, bus_rel_fld($trap_save_priv, +4, FLD_SRC, 1), bus_(HYPER,3<<8|HYPER_SLL8B), 1);
  Emit(ctx, bus_(&RV->MSTATUS, 1), bus_(HYPER, HYPER_OR), 1);
  Emit(ctx, bus(&RV->PRIV), Const32(RV_PRIV_M), 4);

  busad $trap_save_mie = Label(ctx);
  Emit(ctx, bus_rel_fld($trap_save_mie, +1, FLD_SRC, 1), bus_(&RV->MSTATUS, 0), 1);
  Emit(ctx, bus_(&RV->MSTATUS, 0), bus_(SUPER, SUPERV_HandleMPIEOnTrap), 1);
}

void
riscv_build_emulator(emit_ctx *ctx)
{
  volatile uint32_t *rs1 = &RV_Decoder.RS[0], *rs2 = &RV_Decoder.RS[1];

  busad $all_ret_pcnext, $all_ret_direct;
  busad $illegal;

  busad $alu_lui, $alu_auipc, $alu_jal, $alu_jalr, $alu_bcc, $alu_load, $alu_store, $alu_imm_arith,
    $alu_reg_arith, $alu_env, $alu_fence, $alu_atomic;

  busad $emu_custom_0, $alu_custom_bulkmem, $emu_custom_2, $emu_custom_3;
  busad $emu_ifetch;

  {
    Emit(ctx, bus(&RV->EMU_STATE), bus(&K_EMU_STATE_RUNNING), 4);
  }

  // --- Instruction fetcher
  {
    $emu_ifetch = Label(ctx);

    busad $ifetch_load = Label(ctx);
    Emit(ctx, bus_rel_fld($ifetch_load, +1, FLD_SRC, 0), bus(&RV->PC), 4);
    Emit(ctx, bus(&RV_Decoder.inst), bus_null(), 4);
  }

  // --- First stage decoder
  // opcode lives in 6:0 for all types of instructions, and the lowest two bits are 11 for normal
  // instructions, so we can discard those. We use a special OP5_MASK table for extracting said
  // bits, and then branch via the DISPATCH_STAGE_1 table.
  // XXX: If the low bits are _not_ 11, then we branch to $illegal instead.
  //
  // EXT(C): need to take the low bits as well
  volatile uint32_t *dispatch_op7_hi5 = arena_alloc(&ARENA, 256, 256);
  {
    busad $0 = Label(ctx);
    Emit(ctx, bus_rel_fld($0, +1, FLD_SRC, 1), bus(&RV_Decoder.inst), 1);
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 0), bus_(SUPER, SUPERV_OP5_MASK), 1);
    Emit(ctx, bus_rel_fld($0, +3, FLD_NXT, 0), bus(dispatch_op7_hi5), 4);
    MARK(MARK_FST + 1);
  }

  // --- SECTION: Primary Thread-Join
  // Handles:
  //  - PC forward motion
  //  - timer interrupts
  //  - debug timeout
  //  - external interrupts
  //  - etc.
  {
    // Basically, after an instruction retires, control returns here for dispatch.
    // However, one of two things can happen:
    //  1. add 4 to PC, continue
    //  2. don't change PC, continue
    // (1) is used in the typical path, and (2) is used when e.g. branching and jumping
    //
    // EXT(C): increment isn't necessarily 4
    $all_ret_pcnext = Label(ctx);
    Inc4(ctx, 4, bus(&RV->PC), bus(&RV->PC));

    $all_ret_direct = Label(ctx);

    Inc(ctx, 8, bus(&RV->CYCLE64), bus(&RV->CYCLE64));
    // The time increment for the timer is not defined by the spec; therefore! this is (mostly) ok
    // TODO: Actually, we can just hook this into the system timer? Although this may cause problems
    //       for Linux because it has expectations about clock rate.
    // EDIT: Indeed hooking it into the system timer.
    // Inc(ctx, 8, bus(&RV->TIMER64), bus(&RV->TIMER64));

    // --- SUBSECTION: Debug timeout
    //
    // debug_limit_b = RV->DEBUG_CYCLE_LIMIT == ZERO XXX (ICF_X4|ICF_INV)
    // debug_ge_b = RV->CYCLE >= RV->DEBUG_CYCLE_LIMIT XXX (ICF_X4)
    // debug_stop_b = And(1, debug_limit_b, debug_ge_b)
    // $debug_timeout_src_patch = LABEL
    // Emit(REL($debug_timeout_src_patch,+1,FLD_SRC,0), debug_stop_b, 1)
    // $debug_timeout_dispatch = LABEL
    // Emit(REL($debug_timeout_dispatch,+1,FLD_NXT,0), bus(DEBUG_TIMEOUT_SWITCH), 4)
    // MARK(MARK_FST+4)
    // DEBUG_TIMEOUT_SWITCH[1] = LABEL
    // Emit(RV_Decoder.EMU_STATE, _EMU_STATE_DEBUG_TIMEOUT, 4)
    // End()
    // DEBUG_TIMEOUT_SWITCH[0] = LABEL

    // --- SUBSECTION: Interrupt controller
    //  1. External interrupts (UART)
    //  2. Internal interrupts (Timer)
    // Fall-through is $emu_ifetch

    // TODO: also CSR write handling?
    
    Branch(ctx, $emu_ifetch);
  }

  // --- SECTION: Illegal instruction handling
  {
    $illegal = Label(ctx);

    Emit(ctx, bus(&RV->MEPC), bus(&RV->PC), 4);
    Emit(ctx, bus(&RV->MCAUSE), Const32(MCAUSE_ILLEGAL), 4);
    Emit(ctx, bus(&RV->MTVAL), bus(&RV_Decoder.inst), 4);
    Emit(ctx, bus(&RV->PC), bus(&RV->MTVEC), 4);

    RVTrapEntry(ctx);

    Branch(ctx, $emu_ifetch);
  }

  // --- INSN: LUI : Opcode 01101 11
  {
    volatile uint32_t *out = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4));
    __builtin_memset((void *)out, 0, sizeof *out);

    // 2. Writeback

    busad $lui_wb = Label(ctx);
    Emit(ctx, bus(RV->REGS), bus(out), 4);
    Branch(ctx, $all_ret_pcnext);

    // 1. LUI

    $alu_lui = Label(ctx);

    busad $0 = Label(ctx);
    Emit(ctx, bus_rel_fld($0, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 0), 2);
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), bus_(HYPER, HYPERV_OP_A_RD), 1);
    Emit(ctx, bus_rel_fld($lui_wb, +0, FLD_DST, 0), bus_(SUPER, SUPERV_RegisterMapWrite), 1);

    Emit(ctx, bus_(out, 2), bus_(&RV_Decoder.inst, 2), 2);
    And(ctx, 1, bus_(out, 1), bus_(&RV_Decoder.inst, 1), Const8(0xf0));

    Branch(ctx, $lui_wb);
  }

  // --- INSN: AUIPC : Opcode 00101 11
  {
    volatile uint32_t *out = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4));
    __builtin_memset((void *)out, 0, sizeof *out);

    // 2. Writeback

    busad $auipc_wb = Label(ctx);
    Emit(ctx, bus(RV->REGS), bus(out), 4);
    Branch(ctx, $all_ret_pcnext);

    // 1. AUIPC

    $alu_auipc = Label(ctx);

    busad $0 = Label(ctx);
    Emit(ctx, bus_rel_fld($0, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 0), 2);
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), bus_(HYPER, HYPERV_OP_A_RD), 1);
    Emit(ctx, bus_rel_fld($auipc_wb, +0, FLD_DST, 0), bus_(SUPER, SUPERV_RegisterMapWrite), 1);

    // out = (INST & 0xff_ff_f0_00) + PC
    // TODO: optimize
    Emit(ctx, bus_(out, 2), bus_(&RV_Decoder.inst, 2), 2);
    And(ctx, 1, bus_(out, 1), bus_(&RV_Decoder.inst, 1), Const8(0xf0));
    Add(ctx, 4, bus(out), bus(out), bus(&RV->PC));
    Branch(ctx, $auipc_wb);
  }

  // --- INSN: JAL : Opcode 11011 11
  {
    volatile uint32_t *out = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4));
    __builtin_memset((void *)out, 0, sizeof *out);

    busad $jal_wb = Label(ctx);
    Emit(ctx, bus(RV->REGS), bus(out), 4);

    // Emit(ctx, bus(delta), bus(HYPER), 4); // This is really just JAL
    // Add(ctx, 4, bus(&RV->PC), bus(&RV->PC), bus(delta));

    // Inline the Add(,4,PC,PC,delta) to fold the HYPERV_JAL lookup into the label and get rid of
    // temporary.
    busad $2 = Label(ctx);
    EmitStrided(ctx, bus_rel_fld($2, +2, FLD_SRC, 1), bus(HYPER), 4, 1, 63, 0);
    EmitStrided(ctx, bus_rel_fld($2, +2, FLD_SRC, 2), bus(&RV->PC), 4, 1, 63, 0);
    Emit(ctx, bus_(&RV->PC, 0), bus_(HYPER, HYPER_ADD), 2);
    for (size_t i = 1; i < 4; i++) {
      busad $_1 = Label(ctx);
      Emit(ctx, bus_rel_fld($_1, +1, FLD_SRC, 0), bus_(&RV->PC, i), 1);
      Emit(ctx, bus_(&RV->PC, i), bus(HYPER), (i < 3) ? 2 : 1);
    }
    Branch(ctx, $all_ret_direct);

    $alu_jal = Label(ctx);

    busad $0 = Label(ctx);
    Emit(ctx, bus_rel_fld($0, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 0), 2);
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), bus_(HYPER, HYPERV_OP_A_RD), 1);
    Emit(ctx, bus_rel_fld($jal_wb, +0, FLD_DST, 0), bus_(SUPER, SUPERV_RegisterMapWrite), 1);

    Inc4(ctx, 4, bus(out), bus(&RV->PC));

    // Now the tricky part lol
    // 20 | 10:1 | 11 | 19:12
    // 20 gets replicated all the way down to 19, 11 gets moved down, and 10:1 get moved down
    // Write out HYPER | ((0xff_ff_f0_00 & jal) >> 12) << 4
    Emit(ctx, bus_rel_fld($jal_wb, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 2), 2);
    busad $3 = Label(ctx);
    Emit(ctx, bus_rel_fld($3,+1,FLD_SRC,2), bus_(&RV_Decoder.inst, 1), 1);
    Emit(ctx, bus_rel_fld($jal_wb, +1, FLD_SRC, 0), bus_(HYPER, 0xf0<<8|HYPER_AND), 1);
    // And(ctx, 1, bus_rel_fld($jal_wb, +1, FLD_SRC, 0), bus_(&RV_Decoder.inst, 0), Const8(0xf0));
    Branch(ctx, $jal_wb);
  }

  // --- INSN: JALR : Opcode 11001 11
  {
    volatile uint32_t *out = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4)),
                      *tmp = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4));
    __builtin_memset((void *)out, 0, sizeof *out);
    __builtin_memset((void *)tmp, 0, sizeof *tmp);

    // Operation:
    // t =pc+4; pc=(x[rs1]+sext(offset))&∼1; x[rd]=t

    busad $jalr_wb = Label(ctx);
    Emit(ctx, bus(RV->REGS), bus(out), 4);
    Branch(ctx, $all_ret_direct);

    $alu_jalr = Label(ctx);

    busad $0 = Label(ctx);
    Emit(ctx, bus_rel_fld($0, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 0), 2);
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), bus_(HYPER, HYPERV_OP_A_RD), 1);
    Emit(ctx, bus_rel_fld($jalr_wb, +0, FLD_DST, 0), bus_(SUPER, SUPERV_RegisterMapWrite), 1);

    // Rd is written with pc+4
    Inc4(ctx, 4, bus(out), bus(&RV->PC));

    // Load rs1 (set_w by preceding decoder)
    busad $1 = Label(ctx);
    Emit(ctx, bus_rel_fld($1, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 1), 2);
    Emit(ctx, bus_rel_fld($1, +2, FLD_SRC, 0), bus_(HYPER, HYPERV_OP_B_RS1_X4), 1);
    Emit(ctx, bus(rs1), bus(RV->REGS), 4);

    // tmp <- HYPER[(inst>>16)<<8|HYPERV_OP_IMM12_SEXTW]
    // tgt <- tmp + rs1
    // tgt[0] <- tgt[0] & 0xfe
    // pc <- tgt
    busad $3 = Label(ctx);
    Emit(ctx, bus_rel_fld($3, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 2), 2);
    Emit(ctx, bus(tmp), bus_(HYPER, HYPERV_OP_IMM12_SEXTW), 4);
    Add(ctx, 4, bus(&RV->PC), bus(tmp), bus(rs1));
    busad $4 = Label(ctx);
    Emit(ctx, bus_rel_fld($4, +1, FLD_SRC, 2), bus(&RV->PC), 1);
    Emit(ctx, bus(&RV->PC), bus_(HYPER, 0xfe << 8 | HYPER_AND), 1);
    Branch(ctx, $jalr_wb);
  }

  // --- INSN: Bcc : Opcode 11000 11
  // Differentiated on funct3:
  //  0 -> beq
  //  1 -> bne
  //  4 -> blt
  //  5 -> bge
  //  6 -> bltu
  //  7 -> bgeu
  {
    // 4. Branch taken machinery
    //
    // Note that it's only here that we calculate the branch target address, since that's expensive

    busad $branch_taken = Label(ctx);

    // 12 | 10:5 @ 25
    // 4:1 | 11 @ 7
    // rs2, rs1, funct3, "rd" (not actually rd but same slot)
    // basic idea is read xxx | 12 | x | 10:5 | xxxxx from top byte by table, 4:1|11 using the RD
    // table, then map to xxxx | 11 | xxxxxx | 4 : 1 | x by table, and OR the two together
    volatile uint32_t *bhi = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4)),
                      *blo = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4));
    busad $b_12__10_5 = Label(ctx);
    Emit(ctx, bus_rel_fld($b_12__10_5, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 3), 1);
    Emit(ctx, bus(bhi), bus_(SUPER, SUPERV_BccRead_12__10_5), 4);
    busad $b_11__4_1 = Label(ctx);
    Emit(ctx, bus_rel_fld($b_11__4_1, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 0), 2);
    Emit(ctx, bus_rel_fld($b_11__4_1, +2, FLD_SRC, 1), bus_(HYPER, HYPERV_OP_A_RD), 1);
    Emit(ctx, bus(blo), bus_(SUPER, SUPERV_BccRead_11__4_1), 2);

    // TODO: could optimize out blo
    Or(ctx, 2, bus(bhi), bus(bhi), bus(blo));
    Add(ctx, 4, bus(&RV->PC), bus(&RV->PC), bus(bhi));
    Branch(ctx, $all_ret_direct);

    // 3. Taken/Not taken switch

    volatile uint32_t *branch_switch = arena_alloc(&ARENA, 8, 256);
    branch_switch[0] = $all_ret_pcnext._0;
    branch_switch[1] = $branch_taken._0;

    busad $decider = Label(ctx);
    busad $decider_target = bus_rel_fld($decider, +0, FLD_SRC, 0);
    Emit(ctx, bus_rel_fld($decider, +1, FLD_NXT, 0), bus(branch_switch), 4);
    MARK(MARK_BCC);

    // 2. Comparisons

    volatile uint32_t *dispatch_bcc = arena_alloc(&ARENA, 256, 256);
    for (int i = 0; i < 64; i++)
      dispatch_bcc[i] = $illegal._0;

    set_w(dispatch_bcc, 0, Label(ctx)); // BEQ
    Equ(ctx, 4, $decider_target, bus(rs1), bus(rs2), ICF_X4);
    Branch(ctx, $decider);

    set_w(dispatch_bcc, 1, Label(ctx)); // BNE
    Neq(ctx, 4, $decider_target, bus(rs1), bus(rs2), ICF_X4);
    Branch(ctx, $decider);

    set_w(dispatch_bcc, 4, Label(ctx)); // BLT
    Lts(ctx, 4, $decider_target, bus(rs1), bus(rs2), ICF_X4);
    Branch(ctx, $decider);

    set_w(dispatch_bcc, 5, Label(ctx)); // BGE
    Ges(ctx, 4, $decider_target, bus(rs1), bus(rs2), ICF_X4);
    Branch(ctx, $decider);

    set_w(dispatch_bcc, 6, Label(ctx)); // BLTU
    Ltu(ctx, 4, $decider_target, bus(rs1), bus(rs2), ICF_X4);
    Branch(ctx, $decider);

    set_w(dispatch_bcc, 7, Label(ctx)); // BGEU
    Geu(ctx, 4, $decider_target, bus(rs1), bus(rs2), ICF_X4);
    Branch(ctx, $decider);

    // 1. Dispatch on condition code, load rs1, load rs2

    $alu_bcc = Label(ctx);
    EmitStrided(
      ctx, bus_rel_fld($alu_bcc, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 0), 3, 2, +62, -1);

    busad $0 = Label(ctx);
    // Emit(ctx, bus_rel_fld($0,+1,FLD_SRC,1), bus_(&RV_Decoder.inst,0), 2);
    Emit(ctx, bus_rel_fld($0, +1, FLD_SRC, 0), bus_(HYPER, HYPERV_OP_A_FUNCT3_X4), 1);
    Emit(ctx, bus_rel_fld($0, +5, FLD_NXT, 0), bus(dispatch_bcc), 4);

    busad $1 = Label(ctx);
    // Emit(ctx, bus_rel_fld($1,+1,FLD_SRC,1), bus_(&RV_Decoder.inst,1), 2);
    Emit(ctx, bus_rel_fld($1, +1, FLD_SRC, 0), bus_(HYPER, HYPERV_OP_B_RS1_X4), 1);
    Emit(ctx, bus(rs1), bus(RV->REGS), 4);

    busad $2 = Label(ctx);
    // Emit(ctx, bus_rel_fld($2,+1,FLD_SRC,1), bus_(&RV_Decoder.inst,2), 2);
    Emit(ctx, bus_rel_fld($2, +1, FLD_SRC, 0), bus_(HYPER, HYPERV_OP_C_RS2_X4), 1);
    assert(At(ctx, bus_rel_cb($0, +5)));
    Emit(ctx, bus(rs2), bus(RV->REGS), 4);
  }

  // --- INSN: FENCE (.I) : Opcode 00011 11
  // It turns out that FENCE.I is effectively a big ol' NOP on our architecture, on account of how
  // Literally Everything Is Memory.
  // Plain FENCE I'm less sure of, but I figure it's probably okay?
  //
  // TODO: REVIEW FOR MULTICORE.
  {
    $alu_fence = Label(ctx);
    MARK(MARK_FENCE);
    Branch(ctx, $all_ret_pcnext);
  }

  // --- INSNGROUP: Register Arithmetic : Opcode 01100 11
  // Register arithmetic instructions are differentiated by funct3 and funct7
  //
  // --- INSNGROUP: Immediate Arithmetic : Opcode 00100 11
  {
    volatile uint32_t *dispatch_reg_ar = arena_alloc(&ARENA, 256, 256),
                      *dispatch_imm_ar = arena_alloc(&ARENA, 256, 256),
                      *out = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4));
    for (int i = 0; i < 64; i++) {
      set_w(dispatch_reg_ar, i, $illegal);
      set_w(dispatch_imm_ar, i, $illegal);
    }

    busad $arith_wb = Label(ctx);
    busad $arith_wb_ctrl = bus_rel_fld($arith_wb, +0, FLD_DST, 0);
    Emit(ctx, bus(RV->REGS), bus(out), 4);
    Branch(ctx, $all_ret_pcnext);

    busad $add = Label(ctx);
    Add(ctx, 4, bus(out), bus(rs1), bus(rs2));
    Branch(ctx, $arith_wb);
    busad $sub = Label(ctx);
    Sub(ctx, 4, bus(out), bus(rs1), bus(rs2));
    Branch(ctx, $arith_wb);
    busad $xor = Label(ctx);
    Xor(ctx, 4, bus(out), bus(rs1), bus(rs2));
    Branch(ctx, $arith_wb);
    busad $or = Label(ctx);
    Or(ctx, 4, bus(out), bus(rs1), bus(rs2));
    Branch(ctx, $arith_wb);
    busad $and = Label(ctx);
    And(ctx, 4, bus(out), bus(rs1), bus(rs2));
    Branch(ctx, $arith_wb);
    busad $sll = Label(ctx);
    Sll(ctx, 4, bus(out), bus(rs1), bus(rs2));
    Branch(ctx, $arith_wb);
    busad $srl = Label(ctx);
    Srl(ctx, 4, bus(out), bus(rs1), bus(rs2));
    Branch(ctx, $arith_wb);
    busad $sra = Label(ctx);
    Sra(ctx, 4, bus(out), bus(rs1), bus(rs2));
    Branch(ctx, $arith_wb);
    busad $slt = Label(ctx);
    Lts(ctx, 4, bus(out), bus(rs1), bus(rs2), 0);
    Emit(ctx, bus_(out, 1), bus(ZERO), 3);
    Branch(ctx, $arith_wb);
    busad $sltu = Label(ctx);
    Ltu(ctx, 4, bus(out), bus(rs1), bus(rs2), 0);
    Emit(ctx, bus_(out, 1), bus(ZERO), 3);
    Branch(ctx, $arith_wb);

    busad $mul = Label(ctx);
    UMul(ctx, 4, bus(out), bus(rs1), bus(rs2));
    Branch(ctx, $arith_wb);

    volatile uint64_t *mul_wide = MonomorphizeOn(0, arena_alloc(&ARENA, 8, 8));

    busad $mulh = Label(ctx);
    {
      // Note: unsigned widening multiply needs correction terms -(a<0?b:0) -(b<0?a:0)
      UMulW(ctx, 4, bus(mul_wide), bus(rs1), bus(rs2));
      volatile uint32_t *sign_switch = arena_alloc(&ARENA, 16, 256);
      busad $mulh_adj = Label(ctx);
      Emit(ctx, bus_rel_fld($mulh_adj, +1, FLD_SRC, 1), bus_(rs1, 3), 1);
      Emit(ctx, bus_rel_fld($mulh_adj, +4, FLD_SRC, 1), bus_(SUPER, SUPER_SGNB4), 1); // 4*(rs1<0)
      Emit(ctx, bus_rel_fld($mulh_adj, +3, FLD_SRC, 1), bus_(rs2, 3), 1);
      Emit(ctx, bus_rel_fld($mulh_adj, +4, FLD_SRC, 2), bus_(SUPER, SUPER_SGNB8), 1); // 8*(rs2<0)
      Emit(ctx, bus_rel_fld($mulh_adj, +5, FLD_SRC, 0), bus_(HYPER, HYPER_OR), 1);
      Emit(ctx, bus_rel_fld($mulh_adj, +6, FLD_NXT, 0), bus(sign_switch), 4);
      MARK(MARK_REG + 17);
      sign_switch[0] = Label(ctx)._0; // no correction
      Emit(ctx, bus(out), bus_(mul_wide, 4), 4);
      Branch(ctx, $arith_wb);
      sign_switch[1] = Label(ctx)._0; // rs1<0
      Sub(ctx, 4, bus(out), bus_(mul_wide, 4), bus(rs2));
      Branch(ctx, $arith_wb);
      sign_switch[2] = Label(ctx)._0; // rs2<0
      Sub(ctx, 4, bus(out), bus_(mul_wide, 4), bus(rs1));
      Branch(ctx, $arith_wb);
      sign_switch[3] = Label(ctx)._0; // rs2<0 && rs1<0
      Sub(ctx, 4, bus(out), bus_(mul_wide, 4), bus(rs2));
      Sub(ctx, 4, bus(out), bus(out), bus(rs1));
      Branch(ctx, $arith_wb);
    }

    busad $mulhsu = Label(ctx);
    {
      UMulW(ctx, 4, bus(mul_wide), bus(rs1), bus(rs2));
      volatile uint32_t *sign_switch = arena_alloc(&ARENA, 8, 256);
      busad $mulhsu_adj = Label(ctx);
      Emit(ctx, bus_rel_fld($mulhsu_adj, +1, FLD_SRC, 1), bus_(rs1, 3), 1);
      Emit(ctx, bus_rel_fld($mulhsu_adj, +2, FLD_SRC, 1), bus_(SUPER, SUPER_SGNB4), 1); // 4*(rs1<0)
      Emit(ctx, bus_rel_fld($mulhsu_adj, +3, FLD_NXT, 0), bus(sign_switch), 4);
      MARK(MARK_REG + 18);
      sign_switch[0] = Label(ctx)._0;
      Emit(ctx, bus(out), bus_(mul_wide, 4), 4);
      Branch(ctx, $arith_wb);
      sign_switch[1] = Label(ctx)._0;
      Sub(ctx, 4, bus(out), bus_(mul_wide, 4), bus(rs2));
      Branch(ctx, $arith_wb);
    }

    busad $mulhu = Label(ctx);
    UMulW(ctx, 4, bus(mul_wide), bus(rs1), bus(rs2));
    Emit(ctx, bus(out), bus_(mul_wide, 4), 4);
    Branch(ctx, $arith_wb);

    volatile uint32_t *div_scratch = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4));

    busad $div = Label(ctx);
    SDiv32(ctx, bus(out), bus(div_scratch), bus(rs1), bus(rs2));
    Branch(ctx, $arith_wb);
    busad $rem = Label(ctx);
    SDiv32(ctx, bus(div_scratch), bus(out), bus(rs1), bus(rs2));
    Branch(ctx, $arith_wb);
    busad $divu = Label(ctx);
    UDiv32(ctx, bus(out), bus(div_scratch), bus(rs1), bus(rs2));
    Branch(ctx, $arith_wb);
    busad $remu = Label(ctx);
    UDiv32(ctx, bus(div_scratch), bus(out), bus(rs1), bus(rs2));
    Branch(ctx, $arith_wb);

    // ----- Register arithmetic dispatcher

    // funct3 and funct7 decode. i have funct3x4 and funct7x1, so i use (funct3x4 | funct7x1)<<2
    // funct3x4 values cover 000xxx00
    // funct7 values cover 00x0000x
    enum
    {
      F7_Base = 0,
      F7_ExtM = 0x01,
      F7_BaseAlt = 0x20,
    };
#define dispatch_reg_arith(f3, f7, lbl)                                               \
  static_assert((f7 & 0xc0) == 0,                                                     \
                "current dispatch_reg_ar doesn't handle f7 with upper 2 bits set_w"); \
  dispatch_reg_ar[(f3 | f7)] = lbl._0;

    dispatch_reg_arith(0, 0, $add);
    dispatch_reg_arith(0, 0, $add);
    dispatch_reg_arith(0, F7_BaseAlt, $sub);
    dispatch_reg_arith(4, 0, $xor);
    dispatch_reg_arith(6, 0, $or);
    dispatch_reg_arith(7, 0, $and);
    dispatch_reg_arith(1, 0, $sll);
    dispatch_reg_arith(5, 0, $srl);
    dispatch_reg_arith(5, F7_BaseAlt, $sra);
    dispatch_reg_arith(2, 0, $slt);
    dispatch_reg_arith(3, 0, $sltu);

    dispatch_reg_arith(0, F7_ExtM, $mul);
    dispatch_reg_arith(1, F7_ExtM, $mulh);
    dispatch_reg_arith(2, F7_ExtM, $mulhsu);
    dispatch_reg_arith(3, F7_ExtM, $mulhu);
    dispatch_reg_arith(4, F7_ExtM, $div);
    dispatch_reg_arith(5, F7_ExtM, $divu);
    dispatch_reg_arith(6, F7_ExtM, $rem);
    dispatch_reg_arith(7, F7_ExtM, $remu);
#undef dispatch_reg_arith

    {
      $alu_reg_arith = Label(ctx);

      // Get <rd> and <funct3>.x4
      busad $0 = Label(ctx);
      EmitStridedWith(ctx,
                      TI_DST_INC | TI_2DMODE,
                      bus_rel_fld($0, +1, FLD_SRC, 1),
                      bus_(&RV_Decoder.inst, 0),
                      2,
                      2,
                      30,
                      0);
      cblk *_funct3_loader = Emit(ctx, bus_null(), bus_(HYPER, HYPERV_OP_A_FUNCT3_X4), 1);
      cblk *_rd_loader = Emit(ctx, bus_null(), bus_(HYPER, HYPERV_OP_A_RD), 1);

      // Get <rs1>.x4
      busad $1 = Label(ctx);
      Emit(ctx, bus_rel_fld($1, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 1), 2);
      cblk *_rs1_loader = Emit(ctx, bus_null(), bus_(HYPER, HYPERV_OP_B_RS1_X4), 1);

      // Get <rs2>.x4, <funct7>, and jump through dispatch_reg_ar
      busad $2 = Label(ctx);
      Emit(ctx, bus_rel_fld($2, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 2), 2);
      Emit(ctx, bus_rel_fld($2, +2, FLD_SRC, 2), bus_(HYPER, HYPERV_OP_C_FUNCT7), 1);
      _funct3_loader->dst_addr = bus_rel_fld($2, +2, FLD_SRC, 1);
      Emit(ctx, bus_rel_fld($2, +3, FLD_SRC, 2), bus_(HYPER, HYPER_OR), 1);
      Emit(ctx, bus_rel_fld($2, +4, FLD_SRC, 0), bus_(HYPER, (2 << 8) | HYPER_SLL8B), 1);
      cblk *_dispatch_loader = Emit(ctx, bus_null(), bus(dispatch_reg_ar), 4);  // $2+4
      Emit(ctx, bus_rel_fld($2, +6, FLD_SRC, 1), bus_(&RV_Decoder.inst, 2), 2); // $2+5
      cblk *_rs2_loader = Emit(ctx, bus_null(), bus_(HYPER, HYPERV_OP_C_RS2_X4), 1); // $2+6

      busad $3 = Label(ctx);

      _rd_loader->dst_addr = bus_rel_fld($3, +0, FLD_SRC, 1);
      Emit(ctx, $arith_wb_ctrl, bus_(SUPER, SUPERV_RegisterMapWrite), 1);

      _rs1_loader->dst_addr = bus_rel_fld($3, +1, FLD_SRC, 0);
      Emit(ctx, bus(rs1), bus(RV->REGS), 4);

      _rs2_loader->dst_addr = bus_rel_fld($3, +2, FLD_SRC, 0);
      _dispatch_loader->dst_addr = bus_rel_fld($3, +2, FLD_NXT, 0);
      Emit(ctx, bus(rs2), bus(RV->REGS), 4);
    }

    // ----- Immediate arithmetic dispatcher

    volatile uint8_t *sxxi_jumpmap = arena_alloc(&ARENA, 256, 256);
    for (int i = 0; i < 256; i++) {
      if ((i & 0xfe) == 0x40)
        sxxi_jumpmap[i] = 0;
      else if ((i & 0xfe) == 0)
        sxxi_jumpmap[i] = 4;
      else
        sxxi_jumpmap[i] = 8;
    }
    volatile uint32_t *srxi_jump = arena_alloc(&ARENA, 12, 256);
    srxi_jump[0] = $sra._0;
    srxi_jump[1] = $srl._0;
    srxi_jump[2] = $illegal._0;
    volatile uint32_t *slli_jump = arena_alloc(&ARENA, 12, 256);
    slli_jump[0] = $illegal._0; // no slai
    slli_jump[1] = $sll._0;
    slli_jump[2] = $illegal._0;

    busad $srxi = Label(ctx);
    Emit(ctx, bus_rel_fld($srxi, +1, FLD_SRC, 0), bus_(&RV_Decoder.inst, 3), 1);
    Emit(ctx, bus_rel_fld($srxi, +2, FLD_SRC, 0), bus(sxxi_jumpmap), 1);
    Emit(ctx, bus_rel_fld($srxi, +3, FLD_NXT, 0), bus(srxi_jump), 4);
    MARK(0);
    busad $slli = Label(ctx);
    Emit(ctx, bus_rel_fld($slli, +1, FLD_SRC, 0), bus_(&RV_Decoder.inst, 3), 1);
    Emit(ctx, bus_rel_fld($slli, +2, FLD_SRC, 0), bus(sxxi_jumpmap), 1);
    Emit(ctx, bus_rel_fld($slli, +3, FLD_NXT, 0), bus(slli_jump), 4);
    MARK(0);

#define dispatch_imm_arith(f3, lbl) dispatch_imm_ar[f3] = lbl._0;
    dispatch_imm_arith(0, $add);
    dispatch_imm_arith(4, $xor);
    dispatch_imm_arith(6, $or);
    dispatch_imm_arith(7, $and);
    dispatch_imm_arith(1, $slli); // special handling: $slli above
    dispatch_imm_arith(5, $srxi); // special handling: $srxi above
    dispatch_imm_arith(2, $slt);
    dispatch_imm_arith(3, $sltu);
#undef dispatch_imm_arith

    {
      $alu_imm_arith = Label(ctx);

      busad $0 = Label(ctx);
      EmitStridedWith(ctx,
                      TI_DST_INC | TI_2DMODE,
                      bus_rel_fld($0, +1, FLD_SRC, 1),
                      bus_(&RV_Decoder.inst, 0),
                      2,
                      2,
                      30,
                      0);
      Emit(ctx, bus_rel_fld($0, +3, FLD_SRC, 0), bus_(HYPER, HYPERV_OP_A_FUNCT3_X4), 1);
      Emit(ctx, bus_rel_fld($0, +4, FLD_SRC, 1), bus_(HYPER, HYPERV_OP_A_RD), 1);
      cblk *_dispatch_loader = Emit(ctx, bus_null(), bus(dispatch_imm_ar), 4);
      Emit(ctx, $arith_wb_ctrl, bus_(SUPER, SUPERV_RegisterMapWrite), 1);

      // Get <rs1>.x4
      busad $1 = Label(ctx);
      Emit(ctx, bus_rel_fld($1, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 1), 2);
      Emit(ctx, bus_rel_fld($1, +2, FLD_SRC, 0), bus_(HYPER, HYPERV_OP_B_RS1_X4), 1);
      Emit(ctx, bus(rs1), bus(RV->REGS), 4);

      // Get <rs2>.x4, <funct7>, and jump through dispatch_reg_ar
      busad $2 = Label(ctx);
      Emit(ctx, bus_rel_fld($2, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 2), 2);
      _dispatch_loader->dst_addr = bus_rel_fld($2, +1, FLD_NXT, 0);
      Emit(ctx, bus(rs2), bus_(HYPER, HYPERV_OP_IMM12_SEXTW), 4);
    }
  }

  // TODO: Macro-op fusion for MUL/MULH??
  //
  //   If both the high and low bits of the same product are required, then the recommended code
  //   sequence is: MULH[[S]U] rdh, rs1, rs2; MUL rdl, rs1, rs2 (source register specifiers must be
  //   in same order and rdh cannot be the same as rs1 or rs2). Microarchitectures can then fuse
  //   these into a single multiply operation instead of performing two separate multiplies.
  //
  // For DIV?/REM?
  //
  //   If both the quotient and remainder are required from the same division, the recommended code
  //   sequence is: DIV[U] rdq, rs1, rs2; REM[U] rdr, rs1, rs2 (rdq cannot be the same as rs1 or
  //   rs2). Microarchitectures can then fuse these into a single divide operation instead of
  //   performing two separate divides.

  // --- INSNGROUP: Atomics : Opcode 01011 11
  // funct3/funct5 decode (funct3=2 in all cases)
  {
    volatile uint32_t *out = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4)),
                      *scw_valid = arena_alloc(&ARENA, 8, 256),
                      *dispatch_amo = arena_alloc(&ARENA, 256, 256),
                      *tmp = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4));

    busad $amo_wb = Label(ctx);
    Emit(ctx, bus(RV->REGS), bus(out), 4);
    Branch(ctx, $all_ret_pcnext);

    // Zalrsc

    busad $lrw, $scw;

    {
      $lrw = Label(ctx);
      Emit(ctx, bus_rel_fld($lrw, +1, FLD_SRC, 0), bus(rs1), 4);
      Emit(ctx, bus(out), bus_null(), 4);
      Emit(ctx, bus(&RV->AMO_RESERVED), bus(rs1), 4);
      Branch(ctx, $amo_wb);
    }

    {
      busad $scw_ctrl = Label(ctx);
      Emit(ctx, bus_rel_fld($scw_ctrl, +1, FLD_SRC, 0), bus(scw_valid), 4);
      MARK(MARK_ATOMIC + 41);
      $scw = Label(ctx);
      Equ(ctx, 4, bus_null(), bus(&RV->AMO_RESERVED), bus(rs1), ICF_X4);
      Branch(ctx, $scw_ctrl);

      scw_valid[0] = Label(ctx)._0;
      Emit(ctx, bus(out), Const32(1), 4);
      Branch(ctx, $amo_wb);

      busad $scw_ok = Label(ctx);
      scw_valid[1] = $scw_ok._0;
      Emit(ctx, bus_rel_fld($scw_ok, +1, FLD_DST, 0), bus(rs1), 4);
      Emit(ctx, bus_null(), bus(rs2), 4);
      Emit(ctx, bus(out), bus(ZERO), 4);
      Branch(ctx, $amo_wb);
    }

    // Zaamo

    busad $amoswapw, $amoaddw, $amoandw, $amoorw, $amoxorw, $amomaxw, $amominw, $amomaxuw,
      $amominuw;

    {
      $amoswapw = Label(ctx);
      // rd <- (rs1) | (rs1) <- rs2
      Emit(ctx, bus_rel_fld($amoswapw, +1, FLD_SRC, 0), bus(rs1), 4);
      Emit(ctx, bus(out), bus_null(), 4);
      Emit(ctx, bus_rel_fld($amoswapw, +3, FLD_DST, 0), bus(rs1), 4);
      Emit(ctx, bus_null(), bus(rs2), 4);
      Branch(ctx, $amo_wb);
    }

    {
      $amoaddw = Label(ctx);

      busad $0 = Label(ctx);
      Emit(ctx, bus_rel_fld($0, +1, FLD_SRC, 0), bus(rs1), 4);
      Emit(ctx, bus(out), bus_null(), 4);
      Add(ctx, 4, bus(tmp), bus(rs2), bus(out));
      busad $1 = Label(ctx);
      Emit(ctx, bus_rel_fld($1, +1, FLD_DST, 0), bus(rs1), 4);
      Emit(ctx, bus_null(), bus(tmp), 4);
      Branch(ctx, $amo_wb);
    }

    {
      $amoandw = Label(ctx);

      busad $0 = Label(ctx);
      Emit(ctx, bus_rel_fld($0, +1, FLD_SRC, 0), bus(rs1), 4);
      Emit(ctx, bus(out), bus_null(), 4);
      And(ctx, 4, bus(tmp), bus(rs2), bus(out));
      busad $1 = Label(ctx);
      Emit(ctx, bus_rel_fld($1, +1, FLD_DST, 0), bus(rs1), 4);
      Emit(ctx, bus_null(), bus(tmp), 4);
      Branch(ctx, $amo_wb);
    }

    {
      $amoorw = Label(ctx);

      busad $0 = Label(ctx);
      Emit(ctx, bus_rel_fld($0, +1, FLD_SRC, 0), bus(rs1), 4);
      Emit(ctx, bus(out), bus_null(), 4);
      Or(ctx, 4, bus(tmp), bus(rs2), bus(out));
      busad $1 = Label(ctx);
      Emit(ctx, bus_rel_fld($1, +1, FLD_DST, 0), bus(rs1), 4);
      Emit(ctx, bus_null(), bus(tmp), 4);
      Branch(ctx, $amo_wb);
    }

    {
      $amoxorw = Label(ctx);

      busad $0 = Label(ctx);
      Emit(ctx, bus_rel_fld($0, +1, FLD_SRC, 0), bus(rs1), 4);
      Emit(ctx, bus(out), bus_null(), 4);
      Xor(ctx, 4, bus(tmp), bus(rs2), bus(out));
      busad $1 = Label(ctx);
      Emit(ctx, bus_rel_fld($1, +1, FLD_DST, 0), bus(rs1), 4);
      Emit(ctx, bus_null(), bus(tmp), 4);
      Branch(ctx, $amo_wb);
    }

    busad $amominmaxw_ld = Label(ctx);
    Emit(ctx, bus_null(), bus(RV_Decoder.RS), 4);
    Branch(ctx, $amo_wb);

    {
      $amomaxw = Label(ctx);

      busad $0 = Label(ctx);
      Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 0), bus(rs1), 4);
      Emit(ctx, bus_rel_fld($amominmaxw_ld, +0, FLD_DST, 0), bus(rs1), 4);
      Emit(ctx, bus(rs1), bus_null(), 4);
      Emit(ctx, bus(out), bus(rs1), 4);
      Lts(ctx, 4, bus_rel_fld($amominmaxw_ld, +0, FLD_SRC, 0), bus(rs1), bus(rs2), ICF_X4);
      Branch(ctx, $amominmaxw_ld);
    }

    {
      $amominw = Label(ctx);

      busad $0 = Label(ctx);
      Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 0), bus(rs1), 4);
      Emit(ctx, bus_rel_fld($amominmaxw_ld, +0, FLD_DST, 0), bus(rs1), 4);
      Emit(ctx, bus(rs1), bus_null(), 4);
      Emit(ctx, bus(out), bus(rs1), 4);
      Ges(ctx, 4, bus_rel_fld($amominmaxw_ld, +0, FLD_SRC, 0), bus(rs1), bus(rs2), ICF_X4);
      Branch(ctx, $amominmaxw_ld);
    }

    {
      $amomaxuw = Label(ctx);

      busad $0 = Label(ctx);
      Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 0), bus(rs1), 4);
      Emit(ctx, bus_rel_fld($amominmaxw_ld, +0, FLD_DST, 0), bus(rs1), 4);
      Emit(ctx, bus(rs1), bus_null(), 4);
      Emit(ctx, bus(out), bus(rs1), 4);
      Ltu(ctx, 4, bus_rel_fld($amominmaxw_ld, +0, FLD_SRC, 0), bus(rs1), bus(rs2), ICF_X4);
      Branch(ctx, $amominmaxw_ld);
    }

    {
      $amominuw = Label(ctx);

      busad $0 = Label(ctx);
      Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 0), bus(rs1), 4);
      Emit(ctx, bus_rel_fld($amominmaxw_ld, +0, FLD_DST, 0), bus(rs1), 4);
      Emit(ctx, bus(rs1), bus_null(), 4);
      Emit(ctx, bus(out), bus(rs1), 4);
      Geu(ctx, 4, bus_rel_fld($amominmaxw_ld, +0, FLD_SRC, 0), bus(rs1), bus(rs2), ICF_X4);
      Branch(ctx, $amominmaxw_ld);
    }

    for(int i = 0;i<64;i++)
      dispatch_amo[i] = $illegal._0;

    dispatch_amo[0x00] = $amoaddw._0;
    dispatch_amo[0x01] = $amoswapw._0;
    dispatch_amo[0x02] = $lrw._0;
    dispatch_amo[0x03] = $scw._0;
    dispatch_amo[0x04] = $amoxorw._0;
    dispatch_amo[0x08] = $amoorw._0;
    dispatch_amo[0x0C] = $amoandw._0;
    dispatch_amo[0x10] = $amominw._0;
    dispatch_amo[0x14] = $amomaxw._0;
    dispatch_amo[0x18] = $amominuw._0;
    dispatch_amo[0x1C] = $amomaxuw._0;

    // funct5[i] = (i & 0xf8) >> i

    $alu_atomic = Label(ctx);

    busad $A = Label(ctx);
    Emit(ctx, bus_rel_fld($A, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 0), 2);
    Emit(ctx, bus_rel_fld($A, +2, FLD_SRC, 1), bus_(HYPER, HYPERV_OP_A_RD), 1);
    Emit(ctx, bus_rel_fld($amo_wb, +0, FLD_DST, 0), bus_(SUPER, SUPERV_RegisterMapWrite), 1);
    busad $B = Label(ctx);
    Emit(ctx, bus_rel_fld($B, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 1), 2);
    Emit(ctx, bus_rel_fld($B, +2, FLD_SRC, 0), bus_(HYPER, HYPERV_OP_B_RS1_X4), 1);
    Emit(ctx, bus(rs1), bus(RV->REGS), 4);
    busad $C = Label(ctx);
    Emit(ctx, bus_rel_fld($C, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 2), 2);
    Emit(ctx, bus_rel_fld($C, +2, FLD_SRC, 0), bus_(HYPER, HYPERV_OP_C_RS2_X4), 1);
    Emit(ctx, bus(rs2), bus(RV->REGS), 4);
    busad $dispatch_load = Label(ctx);
    Emit(ctx, bus_rel_fld($dispatch_load, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 3), 1);
    Emit(ctx, bus_rel_fld($dispatch_load, +2, FLD_SRC, 0), bus_(SUPER, SUPERV_Amo_Funct5x4), 1);
    Emit(ctx, bus_rel_fld($dispatch_load, +3, FLD_NXT, 0), bus(dispatch_amo), 4);
    MARK(MARK_ATOMIC + 11);
  }

  // NOTE: Loads and stores get fairly complex here because they need to handle PLIC stuff
  // MMIO regions:
  //  - PLIC (MMIO region size: 0x400_0000)
  //  - CLINT (MMIO region size: 0x1_0000)
  //  - UART
  //
  // Note that the UART can't just be a passthrough to the underlying hardware since the hardware
  // interrupt mechanisms can't really interface with the 'interrupts' being received by the RISC-V.
  //
  // General approach: for address A, define 0x0000_0000-0x00ff_ffff as the MMIO range and intercept
  // accesses to that region. All MMIOs are 4-byte aligned, so it's possible to build a trie as the
  // core lookup structure in the most obvious way.
  //
  // Also note that only LW and SW are allowed for accesses to MMIO regions.

  // --- INSN: Loads : Opcode 00000 11
  // Dispatches on funct3:
  //  0 -> lb   1 -> lh   2 -> lw   4 -> lbu  5 -> lhu
  //
  // l<size><ext> <rd>, <simm12>(<rs1>)
  {
    volatile uint32_t *out = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4));

    busad $lw_wb = Label(ctx);
    Emit(ctx, bus(RV->REGS), bus_null(), 4);
    Branch(ctx, $all_ret_pcnext);

    busad $lbh_wb = Label(ctx);
    Emit(ctx, bus(RV->REGS), bus(out), 4);
    Branch(ctx, $all_ret_pcnext);

    busad $lb, $lh, $lbu, $lhu;
    busad $lw;

    // --- Normal loaders

    {
      busad $lb_ld = Label(ctx);
      Emit(ctx, bus(out), bus_null(), 1);
      Emit(ctx, bus_rel_fld($lb_ld, +2, FLD_SRC, 1), bus_(out, 0), 1);
      EmitStridedWith(
        ctx, TI_DST_INC | TI_2DMODE, bus_(out, 1), bus_(SUPER, SUPER_SEXTB), 3, 1, 0, 0);
      Branch(ctx, $lbh_wb);
      $lb = Label(ctx);
      Add(ctx, 4, bus_rel_fld($lb_ld, +0, FLD_SRC, 0), bus(rs1), bus(rs2));
      Branch(ctx, $lb_ld);
    }

    {
      busad $lh_ld = Label(ctx);
      Emit(ctx, bus(out), bus_null(), 2);
      Emit(ctx, bus_rel_fld($lh_ld, +2, FLD_SRC, 1), bus_(out, 1), 1);
      EmitStridedWith(
        ctx, TI_DST_INC | TI_2DMODE, bus_(out, 2), bus_(SUPER, SUPER_SEXTB), 2, 1, 0, 0);
      Branch(ctx, $lbh_wb);
      $lh = Label(ctx);
      Add(ctx, 4, bus_rel_fld($lh_ld, +0, FLD_SRC, 0), bus(rs1), bus(rs2));
      Branch(ctx, $lh_ld);
    }

    {
      busad $lbu_ld = Label(ctx);
      Emit(ctx, bus(out), bus_null(), 1);
      Emit(ctx, bus_(out, 1), bus(ZERO), 3);
      Branch(ctx, $lbh_wb);
      $lbu = Label(ctx);
      Add(ctx, 4, bus_rel_fld($lbu_ld, +0, FLD_SRC, 0), bus(rs1), bus(rs2));
      Branch(ctx, $lbu_ld);
    }

    {
      busad $lhu_ld = Label(ctx);
      Emit(ctx, bus(out), bus_null(), 2);
      Emit(ctx, bus_(out, 2), bus(ZERO), 2);
      Branch(ctx, $lbh_wb);
      $lhu = Label(ctx);
      Add(ctx, 4, bus_rel_fld($lhu_ld, +0, FLD_SRC, 0), bus(rs1), bus(rs2));
      Branch(ctx, $lhu_ld);
    }

    // --- XXX --- XXX --- XXX --- XXX --- XXX --- XXX --- XXX --- XXX --- XXX ---
    // --- ABNORMAL LOADER --- MMIO SUPPORT ---

    {
      // Address being loaded from. Used for a lot of things.
      volatile uint32_t *lw_addr = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4));

      // This will generate a load access fault if bad MMIO occurs
      busad $load_access_fault = Label(ctx);
      Emit(ctx, bus(&RV->MEPC), bus(&RV->PC), 4);
      Emit(ctx, bus(&RV->MCAUSE), Const32(MCAUSE_LOAD_ACCESS), 4);
      Emit(ctx, bus(&RV->MTVAL), bus(lw_addr), 4);
      Emit(ctx, bus(&RV->PC), bus(&RV->MTVEC), 4);
      RVTrapEntry(ctx);
      Branch(ctx, $emu_ifetch);

      // This is a radix trie to match on MMIO addresses.

      static volatile uint8_t trie2_badtrie[256];
      static volatile uint32_t trie3_badtrie_map[1];
      memset((uint8_t *)trie2_badtrie, 0, 256);
      trie3_badtrie_map[0] = $load_access_fault._0;

      uint8_t *trie1, *trie2;
      uint32_t *trie1_map, *trie2_map;
      uint32_t *trie3_p;

      // --- --- TRIE LEVEL 1 (MASK 0xff_00_00)

      trie1 = arena_alloc(&ARENA, 256, 256);
      trie1_map = arena_alloc(&ARENA, 20, 32);
      uint8_t trie1_map_antimodulus = 0xff & (uintptr_t)trie1_map;
      memset(trie1, trie1_map_antimodulus + 0x00, 256);
      trie1[0x10] = trie1_map_antimodulus + 0x04;
      trie1[0x40] = trie1_map_antimodulus + 0x08;
      trie1[0x60] = trie1_map_antimodulus + 0x0c;
      trie1[0x80] = trie1_map_antimodulus + 0x10;

      // --- --- TRIE LEVEL 2 (MASK 0x00_ff_00)

      enum
      {
        trie2_len = 4 * 256,
      };
      trie2 = arena_alloc(&ARENA, trie2_len, 256);
      trie2_map = arena_alloc(&ARENA, 0x60, 256);

      trie1_map[0] = bus(trie2_badtrie)._0;
      for (int i = 0; i < 4; i++)
        trie1_map[1 + i] = bus(trie2 + i * 0x1'00)._0;

      memset(trie2, 0, 256 * 4);
      trie2[0x0'40] = 0x08; // mtimecmp page
      trie2[0x0'bf] = 0x04; // mtime page
      for (int i = 0; i < 16; i++)
        trie2[0x1'00 + i] = 0xc + (i) * 0x04; // prio page 0
      trie2[0x1'10] = 0x4c;                       // pending
      trie2[0x1'20] = 0x50;                       // enable
      trie2[0x2'00] = 0x54;                       // threshold and claim 0
      trie2[0x3'00] = 0x58;                       // pl011 registers
      trie2[0x3'0f] = 0x5c;                       // pl011 ID registers

      // --- --- TRIE LEVEL 3 (MASK 0x00_00_ff)

      enum
      {
        T3_MTIME = 0,
        T3_MTIMECMP = 1,
        T3_PRIO_0 = 2,
        T3_PRIO_15 = 17,
        T3_PENDING = 18,
        T3_ENABLE_01 = 19,
        T3_CONTEXT0 = 20,
        T3_UARTBASE = 21,
        T3_UARTID = 22,
      };
      enum
      {
        trie3_len = 24 * 256,
      };
      trie3_p = arena_alloc(&ARENA, trie3_len, 256);
      trie2_map[0] = bus(trie3_badtrie_map)._0;
      for (int i = 0; i < 24; i++)
        trie2_map[1 + i] = bus(trie3_p + i * 0x1'00)._0;

      for (int i = 0; i < trie3_len / 4; i++)
        trie3_p[i] = $load_access_fault._0;

      busad $lw_wb_ctrl = $lw_wb_ctrl;

      // busad $pending__0_31;
      // busad $enable0__0_31;
      // busad $enable1__0_31;
      // busad $threshold0, $claim0;
      // busad $threshold1, $claim1;
      // busad $uartdr, $uartrsrecr, $uartfr, $uartilpr, $uartibrd, $uartfbrd, $uartlcr_h, $uartcr,
      //   $uartifls, $uartimsc, $uartris, $uartmis, $uarticr, $uartdmacr;
      // busad $uartperiphid0, $uartperiphid1, $uartperiphid2, $uartperiphid3;
      // busad $uartcellid0, $uartcellid1, $uartcellid2, $uartcellid3;

      // -0- XXX -0- XXX -0- XXX -0- XXX -0- XXX -0- XXX -0- XXX -0- XXX -0- XXX -0- XXX -0- XXX -0-

#define mmio_passthru_32(label, ad)       \
  label = Label(ctx);                     \
  Emit(ctx, $lw_wb_ctrl, Const32(ad), 4); \
  Branch(ctx, $lw_wb);
#define mmio_passthru_ad(label, ad)       \
  label = Label(ctx);                     \
  Emit(ctx, $lw_wb_ctrl, ConstAd(ad), 4); \
  Branch(ctx, $lw_wb);

      // ---- ACLINT ----

      busad $mtimelo, $mtimehi, $mtimecmplo, $mtimecmphi;

      mmio_passthru_32($mtimelo, 0x7e003004);
      mmio_passthru_32($mtimehi, 0x7e003008);
      mmio_passthru_ad($mtimecmplo, &RV->MTIMECMPLO);
      mmio_passthru_ad($mtimecmphi, &RV->MTIMECMPHI);

      // ---- PLIC ----

      busad $prio_uart;
      busad $pending__0_31;
      busad $enable0__0_31;
      busad $threshold0, $claim0;

      mmio_passthru_ad($prio_uart, &RV->PLIC_PRIO[1]);
      mmio_passthru_ad($pending__0_31, &RV->PLIC_PENDING[0]);
      mmio_passthru_ad($enable0__0_31, &RV->PLIC_ENABLE_0_31[0]);
      mmio_passthru_ad($threshold0, &RV->PLIC_THRESHOLD[0]);

      $claim0 = Label(ctx);
      // The CPU *makes* a claim by *reading* the PLIC_CLAIMxn register, which will clear the
      // pending bit in the PLIC_PENDINGn register.
      //
      // basic idea: if PENDING[0].1 = 1, then this is a 1 (note that PENDING[0].1 is only set if
      //             PRIORITY[1] is nonzero). PENDING[0].1 will be cleared. MEIP will be cleared in
      //             the MIP CSR.
      //             otherwise return 0 (no other bits of the PENDING register are ever set).
      Emit(ctx, $lw_wb_ctrl, ConstAd(&HYPER[1 << 8 | HYPER_SRL8B]), 4);
      Emit(ctx, bus_add($lw_wb_ctrl, 2), bus(&RV->PLIC_PENDING[0]), 1);
      // XXX: Note: this clears the PENDING[0].0-.7
      Emit(ctx, bus(&RV->PLIC_PENDING[0]), bus(ZERO), 1); // clear PENDING[0].1
      // XXX: Note: this only works because SEIP and LCOFIP are both unimplemented
      Emit(ctx, bus_(&RV->MIP, 1), bus(ZERO), 1);
      // Really should just be:
      // Bic(ctx, 1, bus_(&RV->MIP,1), bus_(&RV->MIP,1), Const8(1 << 3)); // clear MEIP
      Branch(ctx, $lw_wb);

      // ---- PL011 ----

      busad $uartdr, $uartrsrecr, $uartfr, $uartilpr, $uartibrd, $uartfbrd, $uartlcr_h, $uartcr,
        $uartifls, $uartimsc, $uartris, $uartmis, $uarticr, $uartdmacr;
      busad $uartperiphid0, $uartperiphid1, $uartperiphid2, $uartperiphid3;
      busad $uartcellid0, $uartcellid1, $uartcellid2, $uartcellid3;

      mmio_passthru_32($uartdr, 0x7e20'10'00);
      mmio_passthru_32($uartrsrecr, 0x7e20'10'04);
      mmio_passthru_32($uartfr, 0x7e20'10'18);
      mmio_passthru_32($uartilpr, 0x7e20'10'20);
      mmio_passthru_32($uartibrd, 0x7e20'10'24);
      mmio_passthru_32($uartfbrd, 0x7e20'10'28);
      mmio_passthru_32($uartlcr_h, 0x7e20'10'2c);
      mmio_passthru_32($uartcr, 0x7e20'10'30);

      // surprising also passthrough. we hijack the PL011 interrupt mechanisms to generate the PLIC
      // functionality.
      mmio_passthru_32($uartifls, 0x7e20'10'34);
      mmio_passthru_32($uartimsc, 0x7e20'10'38);
      mmio_passthru_32($uartris, 0x7e20'10'3c);
      mmio_passthru_32($uartmis, 0x7e20'10'40);
      mmio_passthru_32($uarticr, 0x7e20'10'44);

      mmio_passthru_32($uartdmacr, 0x7e20'10'48);

      static volatile uint32_t PL011_PERIPHID[4] = { 0x11, 0x10, 0x24, 0x00 };
      static volatile uint32_t PL011_CELLID[4] = { 0x0d, 0xf0, 0x05, 0xb1 };

      mmio_passthru_ad($uartperiphid0, PL011_PERIPHID + 0);
      mmio_passthru_ad($uartperiphid1, PL011_PERIPHID + 1);
      mmio_passthru_ad($uartperiphid2, PL011_PERIPHID + 2);
      mmio_passthru_ad($uartperiphid3, PL011_PERIPHID + 3);

      mmio_passthru_ad($uartcellid0, PL011_CELLID + 0);
      mmio_passthru_ad($uartcellid1, PL011_CELLID + 1);
      mmio_passthru_ad($uartcellid2, PL011_CELLID + 2);
      mmio_passthru_ad($uartcellid3, PL011_CELLID + 3);


#undef mmio_passthru_32
#undef mmio_passthru_ad

      // -0- XXX -0- XXX -0- XXX -0- XXX -0- XXX -0- XXX -0- XXX -0- XXX -0- XXX -0- XXX -0- XXX -0-

#define set_t3(page, byte_address, label) trie3_p[(page * 0x100 + byte_address) >> 2] = label._0
#define set_t3_prio(n, label) trie3_p[((n * 4) + (T3_PRIO_0 * 0x100)) >> 2] = label._0;

      set_t3(T3_MTIME, 0xf8, $mtimelo);
      set_t3(T3_MTIME, 0xfc, $mtimehi);
      set_t3(T3_MTIMECMP, 0x00, $mtimecmplo);
      set_t3(T3_MTIMECMP, 0x04, $mtimecmphi);
      set_t3_prio(1, $prio_uart);
      set_t3(T3_PENDING, 0x00, $pending__0_31);
      set_t3(T3_ENABLE_01, 0x00, $enable0__0_31);
      // set_t3(T3_ENABLE_01, 0x80, $enable1__0_31);
      set_t3(T3_CONTEXT0, 0x00, $threshold0);
      set_t3(T3_CONTEXT0, 0x04, $claim0);
      // set_t3(T3_CONTEXT1, 0x00, $threshold1);
      // set_t3(T3_CONTEXT1, 0x04, $claim1);
      set_t3(T3_UARTBASE, 0x00, $uartdr);
      set_t3(T3_UARTBASE, 0x04, $uartrsrecr);
      set_t3(T3_UARTBASE, 0x18, $uartfr);
      set_t3(T3_UARTBASE, 0x20, $uartilpr);
      set_t3(T3_UARTBASE, 0x24, $uartibrd);
      set_t3(T3_UARTBASE, 0x28, $uartfbrd);
      set_t3(T3_UARTBASE, 0x2c, $uartlcr_h);
      set_t3(T3_UARTBASE, 0x30, $uartcr);
      set_t3(T3_UARTBASE, 0x34, $uartifls);
      set_t3(T3_UARTBASE, 0x38, $uartimsc);
      set_t3(T3_UARTBASE, 0x3c, $uartris);
      set_t3(T3_UARTBASE, 0x40, $uartmis);
      set_t3(T3_UARTBASE, 0x44, $uarticr);
      set_t3(T3_UARTBASE, 0x48, $uartdmacr);
      set_t3(T3_UARTID, 0xe0, $uartperiphid0);
      set_t3(T3_UARTID, 0xe4, $uartperiphid1);
      set_t3(T3_UARTID, 0xe8, $uartperiphid2);
      set_t3(T3_UARTID, 0xec, $uartperiphid3);
      set_t3(T3_UARTID, 0xf0, $uartcellid0);
      set_t3(T3_UARTID, 0xf4, $uartcellid1);
      set_t3(T3_UARTID, 0xf8, $uartcellid2);
      set_t3(T3_UARTID, 0xfc, $uartcellid3);

#undef set_t3_prio
#undef set_t3

      busad $lw_normal = Label(ctx);
      Add(ctx, 4, bus_rel_fld($lw_wb, +0, FLD_SRC, 0), bus(rs1), bus(rs2));
      Branch(ctx, $lw_wb);




      busad $lw_trie_load, $lw_mmio;

      // 3. LW can load from a bunch of different places. If an LW goes to an MMIO region, but the
      //    particular address isn't a real MMIO register, then we are allowed to crash out (we pick
      //    mcause=5).
      {
        $lw_trie_load = Label(ctx);

        Emit(ctx, bus_rel_fld($lw_trie_load, +1, FLD_SRC, 0), bus_(lw_addr, 2), 1);
        Emit(ctx, bus_rel_fld($lw_trie_load, +2, FLD_SRC, 0), bus(trie1), 1);
        Emit(ctx, bus_rel_fld($lw_trie_load, +4, FLD_SRC, 0), bus(trie1_map), 4);

        Emit(ctx, bus_rel_fld($lw_trie_load, +4, FLD_SRC, 0), bus_(lw_addr, 1), 1);
        Emit(ctx, bus_rel_fld($lw_trie_load, +5, FLD_SRC, 0), bus_null(), 1);
        Emit(ctx, bus_rel_fld($lw_trie_load, +7, FLD_SRC, 0), bus(trie2_map), 4);

        // address is multiple of 4 so don't need a two-layer lookup here
        Emit(ctx, bus_rel_fld($lw_trie_load, +7, FLD_SRC, 0), bus_(lw_addr, 0), 1);
        Emit(ctx, bus_rel_fld($lw_trie_load, +8, FLD_NXT, 0), bus_null(), 4);
        MARK(MARK_LOAD + 81);
      }




      // 2. If (addr & 3) != 0, we are allowed to crash out (we pick mcause=4).
      {
        volatile uint32_t *lw_mmio_branch_good = arena_alloc(&ARENA, 8, 256);
        lw_mmio_branch_good[0] = $load_access_fault._0;
        lw_mmio_branch_good[1] = $lw_trie_load._0;

        $lw_mmio = Label(ctx);

        // check if lw_addr is multiple of 4. if not, branch to $badmmio
        Emit(ctx, bus_rel_fld($lw_mmio, +1, FLD_SRC, 1), bus_(lw_addr, 0), 1);
        Emit(ctx, bus_rel_fld($lw_mmio, +2, FLD_SRC, 0), bus_(SUPER, SUPER_4DIV_X4), 1);
        Emit(ctx, bus_rel_fld($lw_mmio, +3, FLD_NXT, 0), bus(lw_mmio_branch_good), 4);
        MARK(MARK_LOAD + 80);
      }




      // 1. LW has special behavior if addr<0x01_00_00_00. If this is the case, we jump into the
      //    machinery that handles loads from MMIO regions. If this is not the case, we jump into
      //    region that handles loads from normal memory.
      //
      // TODO: put protections so that it can crash out if it's still not in DRAM for some reason.

      $lw = Label(ctx);
      {
        volatile uint8_t *dispatch_lw = MonomorphizeOn(0, arena_alloc(&ARENA, 256, 256));
        volatile uint32_t *dispatch_lw_map = arena_alloc(&ARENA, 8, 256);
        memset((void *)dispatch_lw, 4, 256);
        dispatch_lw[0] = 0;
        dispatch_lw_map[0] = $lw_mmio._0;
        dispatch_lw_map[1] = $lw_normal._0;
        Add(ctx, 4, bus(lw_addr), bus(rs1), bus(rs2));

        busad $lw_switcher = Label(ctx);
        Emit(ctx, bus_rel_fld($lw_switcher, +1, FLD_SRC, 0), bus_(lw_addr, 3), 1);
        Emit(ctx, bus_rel_fld($lw_switcher, +2, FLD_SRC, 0), bus(dispatch_lw), 1);
        Emit(ctx, bus_rel_fld($lw_switcher, +3, FLD_NXT, 0), bus(dispatch_lw_map), 4);
        MARK(MARK_LOAD + 70);
      }
    }

    volatile uint32_t *dispatch_load = arena_alloc(&ARENA, 256, 256);
    for (int i = 0; i < 64; i++)
      dispatch_load[i] = $illegal._0;

    dispatch_load[0] = $lb._0;
    dispatch_load[1] = $lh._0;
    dispatch_load[2] = $lw._0;
    dispatch_load[4] = $lbu._0;
    dispatch_load[5] = $lhu._0;

    $alu_load = Label(ctx);
    busad $A = Label(ctx);
    EmitStridedWith(ctx,
                    TI_DST_INC | TI_2DMODE,
                    bus_rel_fld($A, +1, FLD_SRC, 1),
                    bus_(&RV_Decoder.inst, 0),
                    2,
                    2,
                    30,
                    0);
    Emit(ctx, bus_rel_fld($A, +3, FLD_SRC, 0), bus_(HYPER, HYPERV_OP_A_FUNCT3_X4), 1);
    Emit(ctx, bus_rel_fld($A, +4, FLD_SRC, 1), bus_(HYPER, HYPERV_OP_A_RD), 1);
    Emit(ctx, bus_rel_fld($A, +9, FLD_NXT, 0), bus(dispatch_load), 4);
    EmitStridedWith(ctx,
                    TI_DST_INC | TI_2DMODE,
                    bus_rel_fld($lw_wb, +0, FLD_DST, 0),
                    bus_(SUPER, SUPERV_RegisterMapWrite),
                    2,
                    1,
                    31,
                    0);

    busad $B = Label(ctx);
    Emit(ctx, bus_rel_fld($B, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 1), 2);
    Emit(ctx, bus_rel_fld($B, +2, FLD_SRC, 0), bus_(HYPER, HYPERV_OP_B_RS1_X4), 1);
    Emit(ctx, bus(rs1), bus(RV->REGS), 4);

    busad $C = Label(ctx);
    Emit(ctx, bus_rel_fld($C, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 2), 2);
    assert(At(ctx, bus_rel_cb($A, +9)));
    Emit(ctx, bus(rs2), bus_(HYPER, HYPERV_OP_IMM12_SEXTW), 4);
  }

  // --- INSN: Stores : Opcode 01000 11
  // Duspatches on funct3:
  //  0 -> sb   1 -> sh   2 -> sw
  //
  // s<size> <rd>, <simm12>(<rs1>)
  {
  }


  // --- INSNGROUP: Environment (Part 2) :
  {
    // Includes:
    //   funct3=1,2,3, 5,6,7
    //     funct3=1 -> CSRRW
    //     funct3=2 -> CSRRS
    //     funct3=3 -> CSRRC
    //     funct3=5 -> CSRRWI
    //     funct3=6 -> CSRRSI
    //     funct3=7 -> CSRRCI
    //

    $alu_env = Label(ctx);

    busad $ebreak, $ecall_m, $mret, $wfi, $emuctl;

    // --- INSN: ebreak
    {
      $ebreak = Label(ctx);
      Emit(ctx, bus(&RV->MEPC), bus(&RV->PC), 4);
      Emit(ctx, bus(&RV->MCAUSE), Const32(MCAUSE_BREAKPOINT), 4);
      Emit(ctx, bus(&RV->MTVAL), bus(&RV_Decoder.inst), 4);
      Emit(ctx, bus(&RV->PC), bus(&RV->MTVEC), 4);

      RVTrapEntry(ctx);

      Branch(ctx, $emu_ifetch);
    }

    // --- INSN: ecall.M
    {
      $ecall_m = Label(ctx);

      Emit(ctx, bus(&RV->MEPC), bus(&RV->PC), 4);
      // mcause = 8 | PRIV
      busad $ecall_set_cause = Label(ctx);
      Emit(ctx, bus_rel_fld($ecall_set_cause, +1, FLD_SRC, 2), bus(&RV->PRIV), 1);
      Emit(ctx, bus(&RV->MCAUSE), bus_(HYPER, 8 << 8 | HYPER_OR), 1);
      Emit(ctx, bus_(&RV->MCAUSE,1), bus(ZERO), 3);
      Emit(ctx, bus(&RV->MTVAL), bus(ZERO), 4);
      Emit(ctx, bus(&RV->PC), bus(&RV->MTVEC), 4);

      RVTrapEntry(ctx);

      Branch(ctx, $emu_ifetch);
    }

    // --- INSN: mret
    // implementation:
    //  - priv <- MPP from mstatus
    //  - mpie <- MPIE from mstatus
    //  - mstatus' = (mstatus & ~0x1888) | (1 << 7) | mpie as MIE
    //  - pc <- mepc
    {
      enum {
        MSTATUS_BYTE1_MPP_MASK = 3 << 3
      };
      $mret = Label(ctx);
      // load MPP into PRIV.
      // TODO: can probably switch to a SUPER table, but not super high-priority
      Emit(ctx, bus_rel_fld($mret,+1,FLD_SRC,2), bus_(&RV->MSTATUS,1), 1);
      Emit(ctx, bus_rel_fld($mret,+2,FLD_SRC,2), bus_(HYPER,MSTATUS_BYTE1_MPP_MASK<<8|HYPER_AND), 1);
      Emit(ctx, bus(&RV->PRIV), bus_(HYPER,3<<8|HYPER_SRL8B), 1);
      //
      busad $mret_0 = Label(ctx);
      EmitStrided(ctx, bus_rel_fld($mret_0,+1,FLD_SRC,1), bus_(&RV->MSTATUS,0), 2, 1, 31, 0);
      // Emit(ctx, bus_rel_fld($mret_0,+3,FLD_SRC,1), bus_(HYPER,0x88<<8|HYPER_BIC), 1);
      // SUPERV_HandleMPIEOnMRET already handles the 0x88 mask
      Emit(ctx, bus_(&RV->MSTATUS,0), bus_(SUPER, SUPERV_HandleMPIEOnMRET), 1);
      Emit(ctx, bus_(&RV->MSTATUS,1), bus_(HYPER,0xe7<<16|HYPER_AND), 1);

      // MIE is 1<<3, MPIE is 1<<7.
      
      Emit(ctx, bus(&RV->PC), bus(&RV->MEPC), 4);

      // okay, done
      Branch(ctx, $all_ret_direct);
    }

    // --- INSN: wfi
    // implementation: NOP
    {
      $wfi = Label(ctx);
      MARK(MARK_ENV + 11);
      Branch(ctx, $all_ret_pcnext);
    }

    // --- INSN: emuctl
    // implementation: LOL very funny
    {
      $emuctl = Label(ctx);

      Emit(ctx, bus(&RV->EMU_STATE), Const32(EMU_STATE_PASS), 4);
      End(ctx);
    }

    // funct3=0 is most of the diversity of the SYSTEM opcode7 space, so we give it another stage of
    // decode here.
    //   funct3=0
    //     ECALL (Priv 2.1.3.1)
    //      imm12=0
    //     EBREAK (Priv 2.1.3.1)
    //      imm12=1
    //     MRET (Priv 2.1.3.2)
    //      funct7=0x18, rs2=0x2 (imm12=0x302)
    //     WFI (Priv 2.1.3.3)
    //      funct7=0x08, rs2=0x5 (imm12=0x105)
    //     EMU.CTL funct6=101111 funct3=0 (used for emuctl) (Priv 2.1.3.4)
    //      funct7=0x5f|0x5e
    // EMU.CTL is (funct6) 101111 custom11 (funct3)000 custom5 SYSTEM
    //   EMU.CTL.HALT is custom5=0, custom11=0
    //
    // Decode here is whole-imm12
    // Down to map is given in INITIALIZER(rv_decode)
    volatile uint32_t *env0_dispatch_map = arena_alloc(&ARENA, 24, 256);
    env0_dispatch_map[0] = $illegal._0;
    env0_dispatch_map[1] = $ecall_m._0;
    env0_dispatch_map[2] = $ebreak._0;
    env0_dispatch_map[3] = $mret._0;
    env0_dispatch_map[4] = $wfi._0;
    env0_dispatch_map[5] = $emuctl._0;

    busad $env0 = Label(ctx);
    Emit(ctx, bus_rel_fld($env0, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 2), 2);
    Emit(ctx, bus_rel_fld($env0, +2, FLD_SRC, 0), bus_(HYPER, HYPERV_OP_C_ENV0), 1);
    Emit(ctx, bus_rel_fld($env0, +3, FLD_NXT, 0), bus(env0_dispatch_map), 4);
    MARK(MARK_ENV + 2);

    // funct3=1,2,3, 5,6,7 is CSR decode

    volatile uint32_t *zimm = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4)),
                      *csr_src = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4)),
                      *csr_dst = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4)),
                      *csr_num = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4)),
                      *csr_tmp = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4)),
                      *csr_old = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4));

    *zimm = *csr_src = *csr_dst = *csr_num = *csr_tmp = *csr_old = 0;

    busad $csr_wb = Label(ctx);
    Emit(ctx, bus(RV->REGS), bus(csr_old), 4);
    Branch(ctx, $all_ret_pcnext);

    busad $csr_store = Label(ctx);
    Emit(ctx, bus_rel_fld($csr_store, +1, FLD_DST, 0), bus(csr_dst), 4);
    Emit(ctx, bus_null(), bus(rs1), 4);
    Branch(ctx, $csr_wb);

    busad $csrrw = Label(ctx);
    Emit(ctx, bus_rel_fld($csrrw, +1, FLD_SRC, 0), bus(csr_src), 4);
    Emit(ctx, bus(csr_old), bus_null(), 4);
    Branch(ctx, $csr_store);

    busad $csrrs = Label(ctx);
    Emit(ctx, bus_rel_fld($csrrs, +1, FLD_SRC, 0), bus(csr_src), 4);
    Emit(ctx, bus(csr_old), bus_null(), 4);
    Or(ctx, 4, bus(rs1), bus(csr_old), bus(rs1));
    Branch(ctx, $csr_store);

    busad $csrrc = Label(ctx);
    Emit(ctx, bus_rel_fld($csrrc, +1, FLD_SRC, 0), bus(csr_src), 4);
    Emit(ctx, bus(csr_old), bus_null(), 4);
    Bic(ctx, 4, bus(rs1), bus(csr_old), bus(rs1));
    Branch(ctx, $csr_store);

    busad $csri_store = Label(ctx);
    Emit(ctx, bus_rel_fld($csri_store, +1, FLD_DST, 0), bus(csr_dst), 4);
    Emit(ctx, bus_null(), bus(zimm), 4);
    Branch(ctx, $csr_wb);

    busad $csrrwi = Label(ctx);
    Emit(ctx, bus_rel_fld($csrrwi, +1, FLD_SRC, 0), bus(csr_src), 4);
    Emit(ctx, bus(csr_old), bus_null(), 4);
    Branch(ctx, $csri_store);

    busad $csrrsi = Label(ctx);
    Emit(ctx, bus_rel_fld($csrrsi, +1, FLD_SRC, 0), bus(csr_src), 4);
    Emit(ctx, bus(csr_old), bus_null(), 4);
    Or(ctx, 4, bus(zimm), bus(csr_old), bus(zimm));
    Branch(ctx, $csri_store);

    busad $csrrci = Label(ctx);
    Emit(ctx, bus_rel_fld($csrrci, +1, FLD_SRC, 0), bus(csr_src), 4);
    Emit(ctx, bus(csr_old), bus_null(), 4);
    Bic(ctx, 4, bus(zimm), bus(csr_old), bus(zimm));
    Branch(ctx, $csri_store);

    volatile uint32_t *dispatch_env = arena_alloc(&ARENA, 32, 256);
    dispatch_env[0] = $env0._0;
    dispatch_env[1] = $csrrw._0;
    dispatch_env[2] = $csrrs._0;
    dispatch_env[3] = $csrrc._0;
    dispatch_env[4] = $illegal._0;
    dispatch_env[5] = $csrrwi._0;
    dispatch_env[6] = $csrrsi._0;
    dispatch_env[7] = $csrrci._0;

    busad $env_dispatch = Label(ctx);
    Emit(ctx, bus_rel_fld($env_dispatch, +1, FLD_NXT, 0), bus(dispatch_env), 4);
    MARK(MARK_ENV + 1);

    // <rd>, <funct3>x4
    busad $A = Label(ctx);
    EmitStridedWith(ctx,
                    TI_DST_INC | TI_2DMODE,
                    bus_rel_fld($A, +1, FLD_SRC, 1),
                    bus_(&RV_Decoder.inst, 0),
                    2,
                    2,
                    30,
                    0);
    Emit(ctx, bus_rel_fld($env_dispatch, +0, FLD_SRC, 0), bus_(HYPER, HYPERV_OP_A_FUNCT3_X4), 1);
    Emit(ctx, bus_rel_fld($A, +3, FLD_SRC, 1), bus_(HYPER, HYPERV_OP_A_RD), 1);
    Emit(ctx, bus_rel_fld($csr_wb, +0, FLD_DST, 0), bus_(SUPER, SUPERV_RegisterMapWrite), 1);

    // <rs1>, <zimm>
    // TODO: push these down so we don't decode ZIMM for CSRR? and RS1 for CSRR?I
    busad $B = Label(ctx);
    Emit(ctx, bus_rel_fld($B, +1, FLD_SRC, 1), bus_(&RV_Decoder.inst, 1), 2);
    Emit(ctx, bus_rel_fld($B, +2, FLD_SRC, 0), bus_(HYPER, HYPERV_OP_B_RS1_X4), 1);
    Emit(ctx, bus(rs1), bus(RV->REGS), 4);

    Emit(ctx, bus_(zimm, 1), bus(ZERO), 3);
    Emit(ctx, bus_rel_fld($B, +5, FLD_SRC, 1), bus_(&RV_Decoder.inst, 1), 2);
    Emit(ctx, bus_(zimm, 0), bus_(HYPER, HYPERV_OP_B_ZIMM), 1);

    // <csr> stuff. could be more efficient but this shouldn't be in super hot paths.
    busad $C = Label(ctx);
    EmitStridedWith(ctx,
                    TI_DST_INC | TI_2DMODE,
                    bus_rel_fld($C, +1, FLD_SRC, 1),
                    bus_(&RV_Decoder.inst, 2),
                    3,
                    2,
                    30,
                    0);
    Emit(ctx, bus(csr_src), bus_(HYPER, HYPERV_OP_C_CSR_SRC), 4);
    Emit(ctx, bus(csr_dst), bus_(HYPER, HYPERV_OP_C_CSR_DST), 4);
    Emit(ctx, bus(csr_num), bus_(HYPER, HYPERV_OP_C_CSR32), 4);
    Branch(ctx, $env_dispatch);
  }

  // --- INSNGROUP: Custom: bulk memory : Opcode 01010 11
  {
    // R-type decode
    // funct3=0 : memcpy(rd, rs1, rs2). funct7 MUST be 0.
    // funct3=1 : memset(rd, pattern=rs1, rs2). funct7 MUST be 0.
    // All other funct3 values are illegal.

    busad $memcpy, $memset;

    volatile uint32_t *bulk_dst = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4));

    $memcpy = Label(ctx);
    {
      busad $memcpy_patch = Label(ctx);
      Emit(ctx, bus_rel_fld($memcpy_patch,+3,FLD_DST,0), bus(bulk_dst), 4);
      Emit(ctx, bus_rel_fld($memcpy_patch,+3,FLD_SRC,0), bus(rs1), 4);
      Emit(ctx, bus_rel_fld($memcpy_patch,+3,FLD_LEN,0), bus(rs2), 4);
      EmitWith(ctx, TI_DST_INC|TI_SRC_INC, bus_null(), bus_null(), 0);
      Branch(ctx, $all_ret_pcnext);
    }

    $memset = Label(ctx);
    {
      volatile uint8_t *memset_pattern = MonomorphizeOn(0, arena_alloc(&ARENA,16,16));

      busad $memset_patch = Label(ctx);
      EmitStridedWith(ctx, TI_DST_INC|TI_2DMODE, bus(memset_pattern), bus(rs1), 16, 1, 0, 0);
      Emit(ctx,bus_rel_fld($memset_patch,+3,FLD_DST,0), bus(bulk_dst), 4);
      Emit(ctx, bus_rel_fld($memset_patch,+3,FLD_LEN,0), bus(rs2), 4);
      EmitWith(ctx, TI_DST_INC, bus_null(), bus(memset_pattern), 0);
      Branch(ctx, $all_ret_pcnext);
    }
    
    $alu_custom_bulkmem = Label(ctx);
    // dispatch
    volatile uint32_t *dispatch_bulkmem = arena_alloc(&ARENA, 8*4, 256);
    for(int i = 0;i < 8;i++)
      dispatch_bulkmem[i] = $illegal._0;
    dispatch_bulkmem[0] = $memcpy._0;
    dispatch_bulkmem[1] = $memset._0;

    busad $bulkmem_dispatch = Label(ctx);
    Emit(ctx, bus_rel_fld($bulkmem_dispatch,+1,FLD_NXT,0), bus(dispatch_bulkmem), 4);
    MARK(MARK_CUSTOM + 1);
    
    busad $A = Label(ctx);
    EmitStridedWith(ctx, TI_DST_INC|TI_2DMODE, bus_rel_fld($A,+1,FLD_SRC,1), bus_(&RV_Decoder.inst, 0), 2, 2, 30, 0);
    Emit(ctx, bus_rel_fld($bulkmem_dispatch,+0,FLD_SRC,0), bus_(HYPER,HYPERV_OP_A_FUNCT3_X4), 1);
    Emit(ctx, bus_rel_fld($A,+3,FLD_SRC,0), bus_(HYPER, HYPERV_OP_A_RD_X4), 1);
    Emit(ctx, bus(bulk_dst), bus(RV->REGS), 4);

    busad $B = Label(ctx);
    Emit(ctx, bus_rel_fld($B,+1,FLD_SRC,1), bus_(&RV_Decoder.inst, 1), 2);
    Emit(ctx, bus_rel_fld($B,+2,FLD_SRC,0), bus_(HYPER, HYPERV_OP_B_RS1_X4), 1);
    Emit(ctx, bus(rs1), bus(RV->REGS), 4);

    busad $C = Label(ctx);
    Emit(ctx, bus_rel_fld($C,+1,FLD_SRC,1), bus_(&RV_Decoder.inst, 2), 2);
    Emit(ctx, bus_rel_fld($C,+2,FLD_SRC,0), bus_(HYPER, HYPERV_OP_C_RS2_X4), 1);
    Emit(ctx, bus(rs2), bus(RV->REGS), 4);

    Branch(ctx, $bulkmem_dispatch);
  }

  // --- POPULATE FIRST STAGE DECODER (upper 5 bits of Opcode7)

  for (int i = 0; i < 64; i++)
    dispatch_op7_hi5[i] = $illegal._0;
  dispatch_op7_hi5[0b01101] = $alu_lui._0;
  dispatch_op7_hi5[0b00101] = $alu_auipc._0;
  dispatch_op7_hi5[0b11011] = $alu_jal._0;
  dispatch_op7_hi5[0b11001] = $alu_jalr._0;
  dispatch_op7_hi5[0b11000] = $alu_bcc._0;
  dispatch_op7_hi5[0b00000] = $alu_load._0;
  dispatch_op7_hi5[0b01000] = $alu_store._0;
  dispatch_op7_hi5[0b00100] = $alu_imm_arith._0;
  dispatch_op7_hi5[0b01100] = $alu_reg_arith._0;
  dispatch_op7_hi5[0b00011] = $alu_fence._0;
  dispatch_op7_hi5[0b11100] = $alu_env._0;
  dispatch_op7_hi5[0b01011] = $alu_atomic._0;

  // dispatch_op7_hi5[0b00010] = $emu_custom_0._0;
  dispatch_op7_hi5[0b01010] = $alu_custom_bulkmem._0;
  // dispatch_op7_hi5[0b10110] = $emu_custom_2._0;
  // dispatch_op7_hi5[0b11110] = $emu_custom_3._0;

  // Having one op5 that's definitely illegal allows us to intercept instructions with illegal lo2
  // at the OP5_MASK level
  assert(dispatch_op7_hi5[0b11111] == $illegal._0);
}
