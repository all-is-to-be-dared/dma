#if 0

#include "dma-impl.h"
#include "riscv-dma.h"
#include "ctx.h"
#include "stamps.h"
#include "printf.h"

/// Universally available zero value
static volatile uint32_t K_ZERO = 0, K_ONE = 1;

// =================================================================================================
// SECTION: INCREMENT
// =================================================================================================

volatile _Alignas(4 << 8) uint8_t TAB_COINC[4 << 8];
static void tab_init_inc(void) {
  DUMP_TAB_AD(TAB_COINC);
  uint8_t coiloa = bus(TAB_COINC)._0 >> 8, coihia = (bus(TAB_COINC)._0 + (1 << 9)) >> 8;
  for (size_t i = 0; i < 256; i++)
    for (size_t l = 0; l < 2; l++) {
      uint16_t r = i + l;
      TAB_COINC[i + (0 << 8) + (l << 9)] = r & 0xff;
      TAB_COINC[i + (1 << 8) + (l << 9)] = (r >> 8) ? coihia : coiloa;
    }
  return;
}

void staff_stamp_inc8(ctx_t *ctx, bus_t rac, bus_t inp) {
  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +2, FLD_SRC, 1), rac, 1);
  ctx_emit(ctx, bus_rel_fld(_0, +2, FLD_SRC, 0), inp, 1);
  ctx_emit_strided(ctx, rac, bus(TAB_COINC), 2, 1, 0xff);
}

void staff_stamp_inc8_nocry(ctx_t *ctx, bus_t rac, bus_t inp) {
  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +2, FLD_SRC, 1), rac, 1);
  ctx_emit(ctx, bus_rel_fld(_0, +2, FLD_SRC, 0), inp, 1);
  ctx_emit(ctx, rac, bus(TAB_COINC), 1);
}

/// NOTE: does not work if ret == inp
void staff_stamp_inc(ctx_t *ctx, bus_t ret, bus_t inp) {
  static volatile uintptr_t rac_init;
  rac_init = ((bus(TAB_COINC)._0 >> 8) & 255) + 2;
  ctx_emit(ctx, ret, bus(&rac_init), 4);
  staff_stamp_inc8(ctx, bus_add(ret, 0), bus_add(inp, 0));
  staff_stamp_inc8(ctx, bus_add(ret, 1), bus_add(inp, 1));
  staff_stamp_inc8(ctx, bus_add(ret, 2), bus_add(inp, 2));
  staff_stamp_inc8_nocry(ctx, bus_add(ret, 3), bus_add(inp, 3));
}

void staff_stamp_inc64(ctx_t *ctx, bus_t ret, bus_t inp) {
  static volatile uintptr_t rac_init;
  rac_init = ((bus(TAB_COINC)._0 >> 8) & 255) + 2;
  ctx_emit(ctx, ret, bus(&rac_init), 4);
  staff_stamp_inc8(ctx, bus_add(ret, 0), bus_add(inp, 0));
  staff_stamp_inc8(ctx, bus_add(ret, 1), bus_add(inp, 1));
  staff_stamp_inc8(ctx, bus_add(ret, 2), bus_add(inp, 2));
  staff_stamp_inc8(ctx, bus_add(ret, 3), bus_add(inp, 3));
  staff_stamp_inc8(ctx, bus_add(ret, 4), bus_add(inp, 4));
  staff_stamp_inc8(ctx, bus_add(ret, 5), bus_add(inp, 5));
  staff_stamp_inc8(ctx, bus_add(ret, 6), bus_add(inp, 6));
  staff_stamp_inc8_nocry(ctx, bus_add(ret, 7), bus_add(inp, 7));
}

// =================================================================================================
// SECTION: SUPERTABLE
// =================================================================================================

volatile _Alignas(1 << 24) uint8_t TAB_SUPER[1 << 24];
volatile _Alignas(1 << 24) uint8_t TAB_JAL[1 << 24];

static volatile _Alignas(0x100) uint8_t TAB_SEXT_BYTE[0x100];
static void tab_init_sext() {
  DUMP_TAB_AD(TAB_SEXT_BYTE);
  for (int i = 0; i < 256; i++)
    TAB_SEXT_BYTE[i] = (i & 0x80) ? 0xff : 0;
}

// =================================================================================================
// SECTION: SUBTRACTION
// =================================================================================================

static void tab_init_sub() {
  DUMP_SUPERTAB_AD(TAB_SUPER, "(sub)", SUPER_SUB);
  for (uint16_t i = 0; i < 256; i++)
    for (uint16_t j = 0; j < 256; j++)
      for (size_t k = i * 256 + j, l = 0; l < 2; l++) {
        uint16_t r = i - j - l;
        TAB_SUPER[(k << 8) + SUPER_SUB + 2 * l + 0] = r & 0xff;
        TAB_SUPER[(k << 8) + SUPER_SUB + 2 * l + 1] = (r >> 8) ? SUPER_SUB + 2 : SUPER_SUB;
      }
}

void staff_stamp_sub8(ctx_t *ctx, bus_t rac, bus_t lhs, bus_t rhs) {
  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +3, FLD_SRC, 0), rac, 1);
  ctx_emit(ctx, bus_rel_fld(_0, +3, FLD_SRC, 1), rhs, 1);
  ctx_emit(ctx, bus_rel_fld(_0, +3, FLD_SRC, 2), lhs, 1);
  ctx_emit(ctx, rac, bus(TAB_SUPER), 2);
}

void staff_stamp_sub8_nocry(ctx_t *ctx, bus_t rac, bus_t lhs, bus_t rhs) {
  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +3, FLD_SRC, 0), rac, 1);
  ctx_emit(ctx, bus_rel_fld(_0, +3, FLD_SRC, 1), rhs, 1);
  ctx_emit(ctx, bus_rel_fld(_0, +3, FLD_SRC, 2), lhs, 1);
  ctx_emit(ctx, rac, bus(TAB_SUPER), 1);
}

void staff_stamp_sub(ctx_t *ctx, bus_t ret, bus_t lhs, bus_t rhs) {
  static volatile uint32_t rac_init = SUPER_SUB;
  ctx_emit(ctx, ret, bus(&rac_init), 4);
  staff_stamp_sub8(ctx, bus_add(ret, 0), bus_add(lhs, 0), bus_add(rhs, 0));
  staff_stamp_sub8(ctx, bus_add(ret, 1), bus_add(lhs, 1), bus_add(rhs, 1));
  staff_stamp_sub8(ctx, bus_add(ret, 2), bus_add(lhs, 2), bus_add(rhs, 2));
  staff_stamp_sub8_nocry(ctx, bus_add(ret, 3), bus_add(lhs, 3), bus_add(rhs, 3));
}

// =================================================================================================
// SECTION: COMPARISONS
// =================================================================================================

#define _SET_TRUTH_TABLE(base, b0, b1)                                                             \
  do {                                                                                             \
    TAB_POL_TRUTH[base + 0] = b0;                                                                  \
    TAB_POL_TRUTH[base + 1] = b1;                                                                  \
    TAB_ANTIPOL_TRUTH[base + 0] = b1;                                                              \
    TAB_ANTIPOL_TRUTH[base + 1] = b0;                                                              \
    TAB_POL_TRUTH_4[base + 0] = b0 * 4;                                                            \
    TAB_POL_TRUTH_4[base + 1] = b1 * 4;                                                            \
    TAB_ANTIPOL_TRUTH_4[base + 0] = b1 * 4;                                                        \
    TAB_ANTIPOL_TRUTH_4[base + 1] = b0 * 4;                                                        \
  } while (0)

static volatile _Alignas(256) uint8_t TAB_POL_TRUTH[256], TAB_ANTIPOL_TRUTH[256];
static volatile _Alignas(256) uint8_t TAB_POL_TRUTH_4[256], TAB_ANTIPOL_TRUTH_4[256];
static void tab_init_ge() {
  DUMP_TAB_AD(TAB_POL_TRUTH);
  DUMP_TAB_AD(TAB_ANTIPOL_TRUTH);

  DUMP_SUPERTAB_AD(TAB_SUPER, "(geu)", SUPER_GEU);
  for (uint16_t i = 0; i < 256; i++)
    for (uint16_t j = 0; j < 256; j++)
      for (size_t k = i * 256 + j, l = 0; l < 2; l++) {
        uint16_t r = i - j - l;
        TAB_SUPER[(k << 8) + SUPER_GEU + l] = (r >> 8) ? SUPER_GEU + 1 : SUPER_GEU;
      }
  _SET_TRUTH_TABLE(SUPER_GEU, 1, 0);

  DUMP_SUPERTAB_AD(TAB_SUPER, "(eq)", SUPER_EQ);
  for (uint16_t i = 0; i < 256; i++)
    for (uint16_t j = 0; j < 256; j++)
      for (size_t k = (i * 256) + j, l = 0; l < 2; l++)
        // l=1 means 'so far bytes equal'
        TAB_SUPER[(k << 8) + SUPER_EQ + l] = (i == j) && l ? SUPER_EQ + 1 : SUPER_EQ;
  _SET_TRUTH_TABLE(SUPER_EQ, 0, 1);

  DUMP_SUPERTAB_AD(TAB_SUPER, "(ge)", SUPER_GES);
  for (uint16_t i = 0; i < 256; i++)
    for (uint16_t j = 0; j < 256; j++)
      for (size_t k = i * 256 + j, l = 0; l < 4; l++) {
        enum {
          GES_HIGH = SUPER_GES + 0,
          GES_FALSE = SUPER_GES + 1,
          GES_TRUE = SUPER_GES + 2,
          GES_EQ = SUPER_GES + 3,
        };
        int8_t si = i, sj = j;
        uint8_t next;
        switch (SUPER_GES + l) {
        case GES_HIGH:
          next = si < sj ? GES_FALSE : si > sj ? GES_TRUE : GES_EQ;
          break;
        case GES_EQ:
          next = i < j ? GES_FALSE : i > j ? GES_TRUE : GES_EQ;
          break;
        case GES_FALSE:
        case GES_TRUE:
          next = SUPER_GES + l;
          break;
        default:
          __builtin_unreachable();
        }
        TAB_SUPER[(k << 8) + SUPER_GES + l] = next;
      }
  TAB_POL_TRUTH[SUPER_GES + 0] = 0;
  TAB_POL_TRUTH[SUPER_GES + 1] = 0;
  TAB_POL_TRUTH[SUPER_GES + 2] = 1;
  TAB_POL_TRUTH[SUPER_GES + 3] = 1;
  TAB_ANTIPOL_TRUTH[SUPER_GES + 0] = 1;
  TAB_ANTIPOL_TRUTH[SUPER_GES + 1] = 1;
  TAB_ANTIPOL_TRUTH[SUPER_GES + 2] = 0;
  TAB_ANTIPOL_TRUTH[SUPER_GES + 3] = 0;
  TAB_POL_TRUTH_4[SUPER_GES + 0] = 0;
  TAB_POL_TRUTH_4[SUPER_GES + 1] = 0;
  TAB_POL_TRUTH_4[SUPER_GES + 2] = 4;
  TAB_POL_TRUTH_4[SUPER_GES + 3] = 4;
  TAB_ANTIPOL_TRUTH_4[SUPER_GES + 0] = 4;
  TAB_ANTIPOL_TRUTH_4[SUPER_GES + 1] = 4;
  TAB_ANTIPOL_TRUTH_4[SUPER_GES + 2] = 0;
  TAB_ANTIPOL_TRUTH_4[SUPER_GES + 3] = 0;
}

void staff_stamp_cmp_bbb(ctx_t *ctx, bus_t rac, bus_t lhs, bus_t rhs) {
  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +3, FLD_SRC, 0), rac, 1);
  ctx_emit(ctx, bus_rel_fld(_0, +3, FLD_SRC, 1), rhs, 1);
  ctx_emit(ctx, bus_rel_fld(_0, +3, FLD_SRC, 2), lhs, 1);
  ctx_emit(ctx, rac, bus(TAB_SUPER), 1);
}

// writes 1 to ret if lhs >= rhs, else writes 0
void staff_stamp_geu_bww(ctx_t *ctx, bus_t ret, bus_t lhs, bus_t rhs, int flags) {
  static volatile uint32_t rac_init = SUPER_GEU;
  ctx_emit(ctx, ret, bus(&rac_init), 1);
  staff_stamp_cmp_bbb(ctx, bus_add(ret, 0), bus_add(lhs, 0), bus_add(rhs, 0));
  staff_stamp_cmp_bbb(ctx, bus_add(ret, 0), bus_add(lhs, 1), bus_add(rhs, 1));
  staff_stamp_cmp_bbb(ctx, bus_add(ret, 0), bus_add(lhs, 2), bus_add(rhs, 2));
  staff_stamp_cmp_bbb(ctx, bus_add(ret, 0), bus_add(lhs, 3), bus_add(rhs, 3));

  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +1, FLD_SRC, 0), ret, 1);
  ctx_emit(ctx, ret,
           bus((flags & CMP_X4) ? ((flags & CMP_INV) ? TAB_ANTIPOL_TRUTH_4 : TAB_POL_TRUTH_4)
                                : ((flags & CMP_INV) ? TAB_ANTIPOL_TRUTH : TAB_POL_TRUTH)),
           1);
}

void staff_stamp_eq_bww(ctx_t *ctx, bus_t ret, bus_t lhs, bus_t rhs, int flags) {
  static volatile uint32_t rac_init = SUPER_EQ + 1;
  ctx_emit(ctx, ret, bus(&rac_init), 1);
  staff_stamp_cmp_bbb(ctx, bus_add(ret, 0), bus_add(lhs, 0), bus_add(rhs, 0));
  staff_stamp_cmp_bbb(ctx, bus_add(ret, 0), bus_add(lhs, 1), bus_add(rhs, 1));
  staff_stamp_cmp_bbb(ctx, bus_add(ret, 0), bus_add(lhs, 2), bus_add(rhs, 2));
  staff_stamp_cmp_bbb(ctx, bus_add(ret, 0), bus_add(lhs, 3), bus_add(rhs, 3));

  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +1, FLD_SRC, 0), ret, 1);
  ctx_emit(ctx, ret,
           bus((flags & CMP_X4) ? ((flags & CMP_INV) ? TAB_ANTIPOL_TRUTH_4 : TAB_POL_TRUTH_4)
                                : ((flags & CMP_INV) ? TAB_ANTIPOL_TRUTH : TAB_POL_TRUTH)),
           1);
}

void staff_stamp_ge_bww(ctx_t *ctx, bus_t ret, bus_t lhs, bus_t rhs, int flags) {
  static volatile uint32_t rac_init = SUPER_GES;
  ctx_emit(ctx, ret, bus(&rac_init), 1);
  // IMPORTANT: Need to go from MSB to LSB here.
  staff_stamp_cmp_bbb(ctx, bus_add(ret, 0), bus_add(lhs, 3), bus_add(rhs, 3));
  staff_stamp_cmp_bbb(ctx, bus_add(ret, 0), bus_add(lhs, 2), bus_add(rhs, 2));
  staff_stamp_cmp_bbb(ctx, bus_add(ret, 0), bus_add(lhs, 1), bus_add(rhs, 1));
  staff_stamp_cmp_bbb(ctx, bus_add(ret, 0), bus_add(lhs, 0), bus_add(rhs, 0));

  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +1, FLD_SRC, 0), ret, 1);
  ctx_emit(ctx, ret,
           bus((flags & CMP_X4) ? ((flags & CMP_INV) ? TAB_ANTIPOL_TRUTH_4 : TAB_POL_TRUTH_4)
                                : ((flags & CMP_INV) ? TAB_ANTIPOL_TRUTH : TAB_POL_TRUTH)),
           1);
}

// =================================================================================================
// SECTION: ADDITION
// =================================================================================================

// volatile _Alignas(4 << 16) __u8 TAB_COADD[4 << 16];
static void tab_init_add() {
  DUMP_SUPERTAB_AD(TAB_SUPER, "(add)", SUPER_ADD);
  // __u8 coaloa = bus(TAB_COADD)._0 >> 16,
  //      coahia = (bus(TAB_COADD)._0 + (2 << 16)) >> 16;
  for (uint16_t i = 0; i < 256; i++)
    for (uint16_t j = 0; j < 256; j++)
      for (size_t k = i * 256 + j, l = 0; l < 2; l++) {
        uint16_t r = i + j + l;
        TAB_SUPER[(k << 8) + SUPER_ADD + 2 * l + 0] = r & 255;
        TAB_SUPER[(k << 8) + SUPER_ADD + 2 * l + 1] = (r >> 8) ? SUPER_ADD + 2 : SUPER_ADD;
      }
}

void staff_stamp_add8(ctx_t *ctx, bus_t rac, bus_t lhs, bus_t rhs) {
  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +3, FLD_SRC, 0), rac, 1);
  ctx_emit(ctx, bus_rel_fld(_0, +3, FLD_SRC, 1), rhs, 1);
  ctx_emit(ctx, bus_rel_fld(_0, +3, FLD_SRC, 2), lhs, 1);
  ctx_emit(ctx, rac, bus(TAB_SUPER), 2);
}

void staff_stamp_add8_nocry(ctx_t *ctx, bus_t rac, bus_t lhs, bus_t rhs) {
  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +3, FLD_SRC, 0), rac, 1);
  ctx_emit(ctx, bus_rel_fld(_0, +3, FLD_SRC, 1), rhs, 1);
  ctx_emit(ctx, bus_rel_fld(_0, +3, FLD_SRC, 2), lhs, 1);
  ctx_emit(ctx, rac, bus(TAB_SUPER), 1);
}

void staff_stamp_add(ctx_t *ctx, bus_t ret, bus_t lhs, bus_t rhs) {
  static volatile uint32_t rac_init = SUPER_ADD;

  bus_t $0 = ctx_label(ctx);
  ctx_emit_strided( ctx, bus_rel_fld($0,+2,FLD_SRC,1), rhs, 4, 1, _2d_dst_stride(63) );
  ctx_emit_strided( ctx, bus_rel_fld($0,+2,FLD_SRC,2), lhs, 4, 1, _2d_dst_stride(63) );

  ctx_emit(ctx, bus_add(ret, 0), bus_(TAB_SUPER,SUPER_ADD), 2);

  ctx_emit(ctx, bus_rel_fld($0,+4,FLD_SRC,0), bus_add(ret,1), 1);
  ctx_emit(ctx, bus_add(ret, 1), bus(TAB_SUPER), 2);

  ctx_emit(ctx, bus_rel_fld($0,+6,FLD_SRC,0), bus_add(ret,2), 1);
  ctx_emit(ctx, bus_add(ret, 2), bus(TAB_SUPER), 2);

  ctx_emit(ctx, bus_rel_fld($0,+8,FLD_SRC,0), bus_add(ret,3), 1);
  ctx_emit(ctx, bus_add(ret, 3), bus(TAB_SUPER), 1);
}

void staff_stamp_add64(ctx_t *ctx, bus_t ret, bus_t lhs, bus_t rhs) {
  static volatile uint32_t rac_init = SUPER_ADD;
  ctx_emit(ctx, ret, bus(&rac_init), 4);
  staff_stamp_add8(ctx, bus_add(ret, 0), bus_add(lhs, 0), bus_add(rhs, 0));
  staff_stamp_add8(ctx, bus_add(ret, 1), bus_add(lhs, 1), bus_add(rhs, 1));
  staff_stamp_add8(ctx, bus_add(ret, 2), bus_add(lhs, 2), bus_add(rhs, 2));
  staff_stamp_add8(ctx, bus_add(ret, 3), bus_add(lhs, 3), bus_add(rhs, 3));
  staff_stamp_add8(ctx, bus_add(ret, 4), bus_add(lhs, 4), bus_add(rhs, 4));
  staff_stamp_add8(ctx, bus_add(ret, 5), bus_add(lhs, 5), bus_add(rhs, 5));
  staff_stamp_add8(ctx, bus_add(ret, 6), bus_add(lhs, 6), bus_add(rhs, 6));
  staff_stamp_add8_nocry(ctx, bus_add(ret, 7), bus_add(lhs, 7), bus_add(rhs, 7));
}

// =================================================================================================
// SECTION: BINARY NOT
// =================================================================================================

volatile _Alignas(0x100) uint8_t TAB_NOT[0x100];

static void tab_init_not() {
  DUMP_TAB_AD(TAB_NOT);
  for (int i = 0; i < 256; i++)
    TAB_NOT[i] = ~((uint8_t)i);
}

void staff_stamp_not8(ctx_t *ctx, bus_t ret, bus_t arg) {
  bus_t _0 = ctx_label(ctx);
  // emit_strided is about twice as fast as manually striding for this
  ctx_emit(ctx, bus_rel_fld(_0, +1, FLD_SRC, 0), arg, 1);
  ctx_emit(ctx, bus_add(ret, 0), bus(&TAB_NOT), 1);
}

void staff_stamp_not(ctx_t *ctx, bus_t ret, bus_t arg) {
  bus_t _0 = ctx_label(ctx);
  // emit_strided is about twice as fast as manually striding for this
  ctx_emit_strided(ctx, bus_rel_fld(_0, +1, FLD_SRC, 0), arg, 4, 1, 31 << 16);
  ctx_emit(ctx, bus_add(ret, 0), bus(&TAB_NOT), 1);
  ctx_emit(ctx, bus_add(ret, 1), bus(&TAB_NOT), 1);
  ctx_emit(ctx, bus_add(ret, 2), bus(&TAB_NOT), 1);
  ctx_emit(ctx, bus_add(ret, 3), bus(&TAB_NOT), 1);
}

// =================================================================================================
// SECTION: BINARY AND
// =================================================================================================

volatile _Alignas(0x10000) uint8_t TAB_AND[0x10000];

static void tab_init_and() {
  DUMP_TAB_AD(TAB_AND);
  for (int i = 0; i < 256; i++)
    for (int j = 0; j < 256; j++)
      TAB_AND[i * 256 + j] = i & j;
}

void staff_stamp_and8(ctx_t *ctx, bus_t ret, bus_t lhs, bus_t rhs) {
  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +2, FLD_SRC, 0), lhs, 1);
  ctx_emit(ctx, bus_rel_fld(_0, +2, FLD_SRC, 1), rhs, 1);
  ctx_emit(ctx, ret, bus(&TAB_AND), 1);
}

void staff_stamp_and(ctx_t *ctx, bus_t ret, bus_t lhs, bus_t rhs) {
  bus_t _0 = ctx_label(ctx);
  // emit_strided is slightly but not particularly faster than manual striding
  // here (by about ~80 cycles, which is <5% improvement)
  ctx_emit_strided(ctx, bus_rel_fld(_0, +2, FLD_SRC, 0), lhs, 4, 1, _2d_dst_stride(31));
  ctx_emit_strided(ctx, bus_rel_fld(_0, +2, FLD_SRC, 1), rhs, 4, 1, _2d_dst_stride(31));
  ctx_emit(ctx, bus_add(ret, 0), bus(&TAB_AND), 1);
  ctx_emit(ctx, bus_add(ret, 1), bus(&TAB_AND), 1);
  ctx_emit(ctx, bus_add(ret, 2), bus(&TAB_AND), 1);
  ctx_emit(ctx, bus_add(ret, 3), bus(&TAB_AND), 1);
}

// =================================================================================================
// SECTION: BINARY OR
// =================================================================================================

static volatile _Alignas(0x10000) uint8_t TAB_OR[0x10000];

static void tab_init_or() {
  DUMP_TAB_AD(TAB_OR);
  for (int i = 0; i < 256; i++)
    for (int j = 0; j < 256; j++)
      TAB_OR[i * 256 + j] = i | j;
}

void staff_stamp_or8(ctx_t *ctx, bus_t ret, bus_t lhs, bus_t rhs) {
  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +2, FLD_SRC, 0), lhs, 1);
  ctx_emit(ctx, bus_rel_fld(_0, +2, FLD_SRC, 1), rhs, 1);
  ctx_emit(ctx, ret, bus(TAB_OR), 1);
}

void staff_stamp_or(ctx_t *ctx, bus_t ret, bus_t lhs, bus_t rhs) {
  staff_stamp_or8(ctx, bus_add(ret, 0), bus_add(lhs, 0), bus_add(rhs, 0));
  staff_stamp_or8(ctx, bus_add(ret, 1), bus_add(lhs, 1), bus_add(rhs, 1));
  staff_stamp_or8(ctx, bus_add(ret, 2), bus_add(lhs, 2), bus_add(rhs, 2));
  staff_stamp_or8(ctx, bus_add(ret, 3), bus_add(lhs, 3), bus_add(rhs, 3));
}

// =================================================================================================
// SECTION: BINARY XOR
// =================================================================================================

static volatile _Alignas(0x10000) uint8_t TAB_XOR[0x10000];

static void tab_init_xor() {
  DUMP_TAB_AD(TAB_XOR);
  for (int i = 0; i < 256; i++)
    for (int j = 0; j < 256; j++)
      TAB_XOR[i * 256 + j] = i ^ j;
}

void staff_stamp_xor(ctx_t *ctx, bus_t ret, bus_t lhs, bus_t rhs) {
  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +2, FLD_SRC, 0), bus_add(lhs, 0), 1);
  ctx_emit(ctx, bus_rel_fld(_0, +2, FLD_SRC, 1), bus_add(rhs, 0), 1);
  ctx_emit(ctx, bus_add(ret, 0), bus(&TAB_XOR), 1);

  bus_t _1 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_1, +2, FLD_SRC, 0), bus_add(lhs, 1), 1);
  ctx_emit(ctx, bus_rel_fld(_1, +2, FLD_SRC, 1), bus_add(rhs, 1), 1);
  ctx_emit(ctx, bus_add(ret, 1), bus(&TAB_XOR), 1);

  bus_t _2 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_2, +2, FLD_SRC, 0), bus_add(lhs, 2), 1);
  ctx_emit(ctx, bus_rel_fld(_2, +2, FLD_SRC, 1), bus_add(rhs, 2), 1);
  ctx_emit(ctx, bus_add(ret, 2), bus(&TAB_XOR), 1);

  bus_t _3 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_3, +2, FLD_SRC, 0), bus_add(lhs, 3), 1);
  ctx_emit(ctx, bus_rel_fld(_3, +2, FLD_SRC, 1), bus_add(rhs, 3), 1);
  ctx_emit(ctx, bus_add(ret, 3), bus(&TAB_XOR), 1);
}

// =================================================================================================
// SECTION: SHIFT LEFT
// =================================================================================================

static volatile _Alignas(0x800) uint8_t TAB_SLL_SHF[0x800], TAB_SLL_CRY[0x800];
static volatile _Alignas(0x100) uint8_t TAB_SLL_SHF_SEL[0x100], TAB_SLL_CRY_SEL[0x100],
    TAB_SLL_BYTESHIFT[0x100], TAB_SLL_BYTESHIFT_0[0x100], TAB_SLL_BYTESHIFT_1[0x100],
    TAB_SLL_BYTESHIFT_2[0x100], TAB_SLL_BYTESHIFT_3[0x100];

static void tab_init_sll(void) {
  DUMP_TAB_AD(TAB_SLL_SHF_SEL);
  DUMP_TAB_AD(TAB_SLL_CRY_SEL);
  DUMP_TAB_AD(TAB_SLL_SHF);
  DUMP_TAB_AD(TAB_SLL_CRY);
  DUMP_TAB_AD(TAB_SLL_BYTESHIFT);
  DUMP_TAB_AD(TAB_SLL_BYTESHIFT_0);
  DUMP_TAB_AD(TAB_SLL_BYTESHIFT_1);
  DUMP_TAB_AD(TAB_SLL_BYTESHIFT_2);
  DUMP_TAB_AD(TAB_SLL_BYTESHIFT_3);
  for (size_t i = 0; i < 256; i++) {
    TAB_SLL_SHF_SEL[i] = (bus(TAB_SLL_SHF)._0 + (256 * (i & 7))) >> 8;
    TAB_SLL_CRY_SEL[i] = (bus(TAB_SLL_CRY)._0 + (256 * (i & 7))) >> 8;
    uint8_t byte_shift = 4 - ((i & 31) >> 3);
    TAB_SLL_BYTESHIFT[i] = byte_shift;
    TAB_SLL_BYTESHIFT_0[i] = byte_shift + 0;
    TAB_SLL_BYTESHIFT_1[i] = byte_shift + 1;
    TAB_SLL_BYTESHIFT_2[i] = byte_shift + 2;
    TAB_SLL_BYTESHIFT_3[i] = byte_shift + 3;
  }
  for (size_t shift = 0; shift < 8; shift++)
    for (size_t val = 0; val < 256; val++) {
      uint16_t shift_raw = (uint16_t)val << shift;
      TAB_SLL_SHF[256 * shift + val] = shift_raw & 255;
      TAB_SLL_CRY[256 * shift + val] = shift_raw >> 8;
    }
  return;
}

/// 32-bit Shift Left Logical with an 8-bit shift (in the bottom 3 bits of the
/// `shift` param)
void staff_stamp_sll8(ctx_t *ctx, bus_t ret, bus_t in, bus_t shift) {
  static volatile uint32_t shift_table, carry_table, carry;

  shift_table = bus(TAB_SLL_SHF)._0;
  carry_table = bus(TAB_SLL_CRY)._0;

  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +1, FLD_SRC, 0), shift, 1);
  ctx_emit(ctx, bus_(&shift_table, 1), bus(TAB_SLL_SHF_SEL), 1);

  bus_t _1 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_1, +1, FLD_SRC, 0), shift, 1);
  ctx_emit(ctx, bus_(&carry_table, 1), bus(TAB_SLL_CRY_SEL), 1);

  bus_t _3 = ctx_label(ctx);
  ctx_emit_strided_with(ctx, DST_INC | _2DMODE, bus_rel_fld(_3, +2, FLD_SRC, 0), bus(&carry_table), 3, 4,
                        _2d_dst_stride(60));
  ctx_emit(ctx, bus_rel_fld(_3, +2, FLD_SRC, 0), bus_add(in, 0), 1);
  ctx_emit(ctx, bus_(&carry, 1), bus(&K_ZERO), 1);
  ctx_emit(ctx, bus_rel_fld(_3, +4, FLD_SRC, 0), bus_add(in, 1), 1);
  ctx_emit(ctx, bus_(&carry, 2), bus(&K_ZERO), 1);
  ctx_emit(ctx, bus_rel_fld(_3, +6, FLD_SRC, 0), bus_add(in, 2), 1);
  ctx_emit(ctx, bus_(&carry, 3), bus(&K_ZERO), 1);

  bus_t _2 = ctx_label(ctx);
  ctx_emit_strided_with(ctx, DST_INC | _2DMODE, bus_rel_fld(_2, +2, FLD_SRC, 0), bus(&shift_table), 4, 4,
                        _2d_dst_stride(60));
  ctx_emit(ctx, bus_rel_fld(_2, +2, FLD_SRC, 0), bus_add(in, 0), 1);
  ctx_emit(ctx, bus_add(ret, 0), bus(&K_ZERO), 1);
  ctx_emit(ctx, bus_rel_fld(_2, +4, FLD_SRC, 0), bus_add(in, 1), 1);
  ctx_emit(ctx, bus_add(ret, 1), bus(&K_ZERO), 1);
  ctx_emit(ctx, bus_rel_fld(_2, +6, FLD_SRC, 0), bus_add(in, 2), 1);
  ctx_emit(ctx, bus_add(ret, 2), bus(&K_ZERO), 1);
  ctx_emit(ctx, bus_rel_fld(_2, +8, FLD_SRC, 0), bus_add(in, 3), 1);
  ctx_emit(ctx, bus_add(ret, 3), bus(&K_ZERO), 1);


  staff_stamp_or8(ctx, bus_add(ret, 1), bus_add(ret, 1), bus_(&carry, 1));
  staff_stamp_or8(ctx, bus_add(ret, 2), bus_add(ret, 2), bus_(&carry, 2));
  staff_stamp_or8(ctx, bus_add(ret, 3), bus_add(ret, 3), bus_(&carry, 3));
}

void staff_stamp_sll(ctx_t *ctx, bus_t ret, bus_t in, bus_t shift) {
  // staff_stamp_sll8 &t0+4 in[0:4] shift[0:4]
  // (+1:src)[0:1] = shift[0:1]
  // (+2:src)[0:1] = <&tab_byteshift>
  // out[0:4] = <&t0>

  static volatile _Alignas(256) uint32_t t0[2];
  ctx_emit(ctx, bus(t0), bus(&K_ZERO), 4);
  staff_stamp_sll8(ctx, bus_(t0, 4), in, shift);
  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +1, FLD_SRC, 0), shift, 1);
  bus_t _1 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_1, +1, FLD_SRC, 0), bus(TAB_SLL_BYTESHIFT_0), 1);
  ctx_emit(ctx, bus_add(ret, 0), bus(t0), 1);
  bus_t _2 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_2, +1, FLD_SRC, 0), shift, 1);
  bus_t _3 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_3, +1, FLD_SRC, 0), bus(TAB_SLL_BYTESHIFT_1), 1);
  ctx_emit(ctx, bus_add(ret, 1), bus(t0), 1);
  bus_t _4 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_4, +1, FLD_SRC, 0), shift, 1);
  bus_t _5 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_5, +1, FLD_SRC, 0), bus(TAB_SLL_BYTESHIFT_2), 1);
  ctx_emit(ctx, bus_add(ret, 2), bus(t0), 1);
  bus_t _6 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_6, +1, FLD_SRC, 0), shift, 1);
  bus_t _7 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_7, +1, FLD_SRC, 0), bus(TAB_SLL_BYTESHIFT_3), 1);
  ctx_emit(ctx, bus_add(ret, 3), bus(t0), 1);
}

// =================================================================================================
// SECTION: SHIFT RIGHT (LOGICAL)
// =================================================================================================

static volatile _Alignas(0x800) uint8_t TAB_SRL_SHF[0x800], TAB_SRL_CRY[0x800];
static volatile _Alignas(0x100) uint8_t TAB_SRL_SHF_SEL[0x100], TAB_SRL_CRY_SEL[0x100],
    TAB_SRL_BYTESHIFT[0x100];

static void tab_init_srl(void) {
  DUMP_TAB_AD(TAB_SRL_SHF_SEL);
  DUMP_TAB_AD(TAB_SRL_CRY_SEL);
  DUMP_TAB_AD(TAB_SRL_SHF);
  DUMP_TAB_AD(TAB_SRL_CRY);
  DUMP_TAB_AD(TAB_SRL_BYTESHIFT);

  for (size_t i = 0; i < 256; i++) {
    TAB_SRL_SHF_SEL[i] = (bus(TAB_SRL_SHF)._0 + (256 * (i & 7))) >> 8;
    TAB_SRL_CRY_SEL[i] = (bus(TAB_SRL_CRY)._0 + (256 * (i & 7))) >> 8;
    TAB_SRL_BYTESHIFT[i] = (i & 31) >> 3; // Difference: no 4-
  }
  for (size_t shift = 0; shift < 8; shift++)
    for (size_t val = 0; val < 256; val++) {
      uint16_t shift_raw = ((uint16_t)val << 8) >> shift;
      TAB_SRL_SHF[256 * shift + val] = shift_raw >> 8;
      TAB_SRL_CRY[256 * shift + val] = shift_raw & 255;
    }
  return;
}

void staff_stamp_srl8(ctx_t *ctx, bus_t ret, bus_t in, bus_t shift) {
  static volatile uint32_t shift_table, carry_table, carry;

  shift_table = bus(TAB_SRL_SHF)._0;
  carry_table = bus(TAB_SRL_CRY)._0;

  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +1, FLD_SRC, 0), shift, 1);
  ctx_emit(ctx, bus_(&shift_table, 1), bus(TAB_SRL_SHF_SEL), 1);

  bus_t _1 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_1, +1, FLD_SRC, 0), shift, 1);
  ctx_emit(ctx, bus_(&carry_table, 1), bus(TAB_SRL_CRY_SEL), 1);

  bus_t _3 = ctx_label(ctx);
  ctx_emit_strided_with(ctx, DST_INC | _2DMODE, bus_rel_fld(_3, +2, FLD_SRC, 0), bus(&carry_table), 3, 4,
                        _2d_dst_stride(60));
  ctx_emit(ctx, bus_rel_fld(_3, +2, FLD_SRC, 0), bus_add(in, 1), 1);
  ctx_emit(ctx, bus_(&carry, 0), bus(&K_ZERO), 1);
  ctx_emit(ctx, bus_rel_fld(_3, +4, FLD_SRC, 0), bus_add(in, 2), 1);
  ctx_emit(ctx, bus_(&carry, 1), bus(&K_ZERO), 1);
  ctx_emit(ctx, bus_rel_fld(_3, +6, FLD_SRC, 0), bus_add(in, 3), 1);
  ctx_emit(ctx, bus_(&carry, 2), bus(&K_ZERO), 1);

  bus_t _2 = ctx_label(ctx);
  ctx_emit_strided_with(ctx, DST_INC | _2DMODE, bus_rel_fld(_2, +2, FLD_SRC, 0), bus(&shift_table), 4, 4,
                        _2d_dst_stride(60));
  ctx_emit(ctx, bus_rel_fld(_2, +2, FLD_SRC, 0), bus_add(in, 0), 1);
  ctx_emit(ctx, bus_add(ret, 0), bus(&K_ZERO), 1);
  ctx_emit(ctx, bus_rel_fld(_2, +4, FLD_SRC, 0), bus_add(in, 1), 1);
  ctx_emit(ctx, bus_add(ret, 1), bus(&K_ZERO), 1);
  ctx_emit(ctx, bus_rel_fld(_2, +6, FLD_SRC, 0), bus_add(in, 2), 1);
  ctx_emit(ctx, bus_add(ret, 2), bus(&K_ZERO), 1);
  ctx_emit(ctx, bus_rel_fld(_2, +8, FLD_SRC, 0), bus_add(in, 3), 1);
  ctx_emit(ctx, bus_add(ret, 3), bus(&K_ZERO), 1);


  staff_stamp_or8(ctx, bus_add(ret, 0), bus_add(ret, 0), bus_(&carry, 0));
  staff_stamp_or8(ctx, bus_add(ret, 1), bus_add(ret, 1), bus_(&carry, 1));
  staff_stamp_or8(ctx, bus_add(ret, 2), bus_add(ret, 2), bus_(&carry, 2));
}

void staff_stamp_srl(ctx_t *ctx, bus_t ret, bus_t in, bus_t shift) {
  static volatile _Alignas(256) uint32_t t0[2] = {0, 0};
  staff_stamp_srl8(ctx, bus_(t0, 0), in, shift);
  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +1, FLD_SRC, 0), shift, 1);
  bus_t _1 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_1, +1, FLD_SRC, 0), bus(TAB_SRL_BYTESHIFT), 1);
  ctx_emit(ctx, ret, bus(t0), 4);
}

// =================================================================================================
// SECTION: SHIFT RIGHT (ARITHMETIC)
// =================================================================================================

// Okay, surely there's a smarter way to do this than resorting to bitops
// I think it's (mostly) the same as SRL but we need an extra input bit that
// describes the sign? so for aabbccdd>>ss we have
//
// aa -> (shifted_sext,carry)
// {bb,cc,dd} -> (shifted,carry)
//
// then, for multibyte, we have aa -> sext_byte

static volatile _Alignas(0x800) uint8_t TAB_SRA_SHFHI[0x800];
static volatile _Alignas(0x100) uint8_t TAB_SRA_SHFHI_SEL[0x100];

static void tab_init_sra(void) {
  DUMP_TAB_AD(TAB_SRA_SHFHI);
  DUMP_TAB_AD(TAB_SRA_SHFHI_SEL);

  for (size_t i = 0; i < 256; i++) {
    TAB_SRA_SHFHI_SEL[i] = (bus(TAB_SRA_SHFHI)._0 + (256 * (i & 7))) >> 8;
  }
  for (size_t shift = 0; shift < 8; shift++)
    for (size_t val = 0; val < 256; val++) {
      uint16_t val16 = (uint16_t)val << 8;
      uint16_t ushr16 = val16 >> shift;
      uint16_t ushr8 = ushr16 >> 8;
      uint16_t sext16 = (val & 0x80) ? 0xff00 : 0;
      uint16_t sext8 = sext16 >> shift;
      TAB_SRA_SHFHI[256 * shift + val] = ushr8 | sext8;
    }
  assert(TAB_SRA_SHFHI[0x4c0] == 0xfc);
  assert(TAB_SRA_SHFHI[0x470] == 0x07);
}

void staff_stamp_sra8(ctx_t *ctx, bus_t ret, bus_t in, bus_t shift) {
  static volatile uint32_t shift_table_hi, carry, shift_table, carry_table;

  shift_table_hi = bus(TAB_SRA_SHFHI)._0;
  shift_table = bus(TAB_SRL_SHF)._0;
  carry_table = bus(TAB_SRL_CRY)._0;

  bus_t _0 = ctx_label(ctx);
  ctx_emit_strided_with(ctx, DST_INC | _2DMODE, bus_rel_fld(_0, +1, FLD_SRC, 0), shift, 3, 1,
                        _2d_dst_stride(31));
  ctx_emit(ctx, bus_(&shift_table, 1), bus(TAB_SRL_SHF_SEL), 1);
  ctx_emit(ctx, bus_(&shift_table_hi, 1), bus(TAB_SRA_SHFHI_SEL), 1);
  ctx_emit(ctx, bus_(&carry_table, 1), bus(TAB_SRL_CRY_SEL), 1);

  // TODO: shift_table_hi is redundant; we can move directly into the SRC field
  bus_t _1 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_1, +2, FLD_SRC, 0), bus(&shift_table_hi), 4);
  ctx_emit(ctx, bus_rel_fld(_1, +2, FLD_SRC, 0), bus_add(in, 3), 1);
  ctx_emit(ctx, bus_add(ret, 3), bus(&K_ZERO), 1);

  bus_t _2 = ctx_label(ctx);
  ctx_emit_strided_with(ctx, DST_INC | _2DMODE, bus_rel_fld(_2, +2, FLD_SRC, 0), bus(&shift_table), 3, 4,
                        _2d_dst_stride(28));
  ctx_emit_strided(ctx, bus_rel_fld(_2, +2, FLD_SRC, 0), in, 3, 1, _2d_dst_stride(31));
  ctx_emit(ctx, bus_add(ret, 0), bus(&K_ZERO), 1);
  ctx_emit(ctx, bus_add(ret, 1), bus(&K_ZERO), 1);
  ctx_emit(ctx, bus_add(ret, 2), bus(&K_ZERO), 1);

  bus_t _3 = ctx_label(ctx);
  ctx_emit_strided_with(ctx, DST_INC | _2DMODE, bus_rel_fld(_3, +2, FLD_SRC, 0), bus(&carry_table), 3, 4,
                        _2d_dst_stride(28));
  ctx_emit_strided(ctx, bus_rel_fld(_3, +2, FLD_SRC, 0), bus_add(in, 1), 3, 1, _2d_dst_stride(31));
  ctx_emit(ctx, bus_(&carry, 0), bus(&K_ZERO), 1);
  ctx_emit(ctx, bus_(&carry, 1), bus(&K_ZERO), 1);
  ctx_emit(ctx, bus_(&carry, 2), bus(&K_ZERO), 1);

  staff_stamp_or8(ctx, bus_add(ret, 0), bus_add(ret, 0), bus_(&carry, 0));
  staff_stamp_or8(ctx, bus_add(ret, 1), bus_add(ret, 1), bus_(&carry, 1));
  staff_stamp_or8(ctx, bus_add(ret, 2), bus_add(ret, 2), bus_(&carry, 2));
}

void staff_stamp_sra(ctx_t *ctx, bus_t ret, bus_t in, bus_t shift) {
  static volatile _Alignas(256) uint32_t t0[2];
  staff_stamp_sra8(ctx, bus_(t0, 0), in, shift);
  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +1, FLD_SRC, 0), bus_add(in, 3), 1);
  ctx_emit_with(ctx, DST_INC, bus_(t0, 4), bus(TAB_SEXT_BYTE), 4);
  bus_t _1 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_1, +1, FLD_SRC, 0), shift, 1);
  bus_t _2 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_2, +1, FLD_SRC, 0), bus(TAB_SRL_BYTESHIFT), 1);
  ctx_emit(ctx, ret, bus(t0), 4);
}

// =================================================================================================
// SECTION: 2'S COMPLEMENT NEGATION
// =================================================================================================

void staff_stamp_neg(ctx_t *ctx, bus_t ret, bus_t arg) {
  static volatile uint32_t tmp = 0;
  staff_stamp_not(ctx, bus(&tmp), arg);
  staff_stamp_inc(ctx, ret, bus(&tmp));
}

// =================================================================================================
// SECTION: MULTIPLICATION
// =================================================================================================

static void tab_init_mul()
{
  DUMP_SUPERTAB_AD(TAB_SUPER, "(mul)", SUPER_MUL);
  for(int B=0;B<256;B++)
    for(int C=0;C<256;C++) {
      int k = (B<<8|C)<<8;
      TAB_SUPER[k+SUPER_MUL] = (B*C)&0xff;
      TAB_SUPER[k+SUPER_MUL+1] = (B*C)>>8;
    }
}

void stamp_umul_qww(ctx_t *ctx,
                bus_t ret64,
                bus_t lhs32,
                bus_t rhs32)
{

#define AiBj(r,i,j) \
  bus_t $a##i##b##j = ctx_label(ctx); \
  ctx_emit(ctx, bus_rel_fld($a##i##b##j,+2,FLD_SRC,1), a##i, 1); \
  ctx_emit(ctx, bus_rel_fld($a##i##b##j,+2,FLD_SRC,2), b##j, 1); \
  ctx_emit(ctx, r, bus_(TAB_SUPER,SUPER_MUL), 2);
  // clang-format off
  static volatile uint32_t
    t_a0b0,                         /*|*/
    t_a0b1, t_a1b0,                 /*|*/ o_1,
    t_a0b2, t_a1b1, t_a2b0,         /*|*/ o_2a, o_2b,
    t_a0b3, t_a1b2, t_a2b1, t_a3b0, /*|*/ o_3a, o_3b, o_3c,
            t_a1b3, t_a2b2, t_a3b1, /*|*/ o_4a, o_4b,
                    t_a2b3, t_a3b2, /*|*/ o_5a,
                            t_a3b3, /*|*/ o_6;
  // clang-format on
  bus_t a0 = bus_add(lhs32,0), a1 = bus_add(lhs32,1), a2 = bus_add(lhs32,2), a3 = bus_add(lhs32,3);
  bus_t b0 = bus_add(rhs32,0), b1 = bus_add(rhs32,1), b2 = bus_add(rhs32,2), b3 = bus_add(rhs32,3);

  // ff * ff = fe01
  // a0b0                      | maxcry = fe -> fe
  // a0b1 + a1b0               | maxcry = fe + fe.01 + fe.01 (01.fd.00) -> 01.fd
  // a0b2 + a1b1 + a2b0        | maxcry = 01.fd + 3 * fe.01 (02.fc.00) -> 02.fc
  // a0b3 + a1b2 + a2b1 + a3b0 | maxcry = 02.fc + 4 * fe.01 (03.fb.00) -> 03.fb
  //        a1b3 + a2b2 + a3b1 | maxcry = 03.fb + 3 * fe.01 (02.fd.fe) -> 02.fd
  //               a2b3 + a3b2 | maxcry = 02.fd + 2 * fe.01 (01.fe.ff) -> 01.fe
  //                      a3b3 | nonxtcb
  //
  // Note to self: can speed this up (somewhat) by doing adds that read directly from the SUPER
  // table (to some extent)?
  static volatile uint64_t ZERO64 = 0;
  ctx_emit(ctx, ret64, bus(&ZERO64), 8);
  // O0
  AiBj(bus_add(ret64,0), 0, 0);
  // O1
  AiBj(bus(&t_a0b1), 0, 1);
  AiBj(bus(&t_a1b0), 1, 0);
  stamp_add(ctx, bus(&o_1), bus(&t_a0b1), bus_add(ret64, 1));
  stamp_add(ctx, bus_add(ret64,1), bus(&o_1), bus(&t_a1b0));
  // O2
  AiBj(bus(&t_a0b2), 0, 2);
  AiBj(bus(&t_a1b1), 1, 1);
  AiBj(bus(&t_a2b0), 2, 0);
  stamp_add(ctx, bus(&o_2a), bus(&t_a0b2), bus_add(ret64, 2));
  stamp_add(ctx, bus(&o_2b), bus(&o_2a), bus(&t_a1b1));
  stamp_add(ctx, bus_add(ret64,2), bus(&o_2b), bus(&t_a2b0));
  // O3
  AiBj(bus(&t_a0b3), 0, 3);
  AiBj(bus(&t_a1b2), 1, 2);
  AiBj(bus(&t_a2b1), 2, 1);
  AiBj(bus(&t_a3b0), 3, 0);
  stamp_add(ctx, bus(&o_3a), bus(&t_a0b3), bus_add(ret64, 3));
  stamp_add(ctx, bus(&o_3b), bus(&o_3a), bus(&t_a1b2));
  stamp_add(ctx, bus(&o_3c), bus(&o_3b), bus(&t_a2b1));
  stamp_add(ctx, bus_add(ret64,3), bus(&o_3c), bus(&t_a3b0));
  // O4
  AiBj(bus(&t_a1b3), 1, 3);
  AiBj(bus(&t_a2b2), 2, 2);
  AiBj(bus(&t_a3b1), 3, 1);
  stamp_add(ctx, bus(&o_4a), bus(&t_a1b3), bus_add(ret64, 4));
  stamp_add(ctx, bus(&o_4b), bus(&o_4a), bus(&t_a2b2));
  stamp_add(ctx, bus_add(ret64,4), bus(&o_4b), bus(&t_a3b1));
  // O5
  AiBj(bus(&t_a2b3), 2, 3);
  AiBj(bus(&t_a3b2), 3, 2);
  stamp_add(ctx, bus(&o_5a), bus(&t_a2b3), bus_add(ret64, 5));
  stamp_add(ctx, bus_add(ret64,5), bus(&o_5a), bus(&t_a3b2));
  // O6
  AiBj(bus(&t_a3b3), 3, 3);
  stamp_add(ctx, bus(&o_6), bus(&t_a3b3), bus_add(ret64, 6));
  ctx_emit(ctx, bus_add(ret64,6), bus(&o_6), 2);
}

void stamp_umul_www(ctx_t *ctx,
                bus_t ret32,
                bus_t lhs32,
                bus_t rhs32)
{

#define AiBj(r,i,j) \
  bus_t $a##i##b##j = ctx_label(ctx); \
  ctx_emit(ctx, bus_rel_fld($a##i##b##j,+2,FLD_SRC,1), a##i, 1); \
  ctx_emit(ctx, bus_rel_fld($a##i##b##j,+2,FLD_SRC,2), b##j, 1); \
  ctx_emit(ctx, r, bus_(TAB_SUPER,SUPER_MUL), 2);
  // clang-format off
  static volatile uint32_t
    t_a0b0,                         /*|*/
    t_a0b1, t_a1b0,                 /*|*/ o_1,
    t_a0b2, t_a1b1, t_a2b0,         /*|*/ o_2a, o_2b,
    t_a0b3, t_a1b2, t_a2b1, t_a3b0, /*|*/ o_3a, o_3b, o_3c,
            t_a1b3, t_a2b2, t_a3b1, /*|*/ o_4a, o_4b,
                    t_a2b3, t_a3b2, /*|*/ o_5a,
                            t_a3b3, /*|*/ o_6;
  // clang-format on
  bus_t a0 = bus_add(lhs32,0), a1 = bus_add(lhs32,1), a2 = bus_add(lhs32,2), a3 = bus_add(lhs32,3);
  bus_t b0 = bus_add(rhs32,0), b1 = bus_add(rhs32,1), b2 = bus_add(rhs32,2), b3 = bus_add(rhs32,3);

  // ff * ff = fe01
  // a0b0                      | maxcry = fe -> fe
  // a0b1 + a1b0               | maxcry = fe + fe.01 + fe.01 (01.fd.00) -> 01.fd
  // a0b2 + a1b1 + a2b0        | maxcry = 01.fd + 3 * fe.01 (02.fc.00) -> 02.fc
  // a0b3 + a1b2 + a2b1 + a3b0 | maxcry = 02.fc + 4 * fe.01 (03.fb.00) -> 03.fb
  //        a1b3 + a2b2 + a3b1 | maxcry = 03.fb + 3 * fe.01 (02.fd.fe) -> 02.fd
  //               a2b3 + a3b2 | maxcry = 02.fd + 2 * fe.01 (01.fe.ff) -> 01.fe
  //                      a3b3 | nonxtcb
  //
  // Note to self: can speed this up (somewhat) by doing adds that read directly from the SUPER
  // table (to some extent)?
  static volatile uint64_t ZERO64 = 0;
  static volatile uint8_t BUFFER[8];

  ctx_emit(ctx, bus(BUFFER), bus(&ZERO64), 8);
  // O0
  AiBj(bus_(BUFFER,0), 0, 0);
  // O1
  AiBj(bus(&t_a0b1), 0, 1);
  AiBj(bus(&t_a1b0), 1, 0);
  stamp_add(ctx, bus(&o_1), bus(&t_a0b1), bus_(BUFFER, 1));
  stamp_add(ctx, bus_(BUFFER,1), bus(&o_1), bus(&t_a1b0));
  // O2
  AiBj(bus(&t_a0b2), 0, 2);
  AiBj(bus(&t_a1b1), 1, 1);
  AiBj(bus(&t_a2b0), 2, 0);
  stamp_add(ctx, bus(&o_2a), bus(&t_a0b2), bus_(BUFFER, 2));
  stamp_add(ctx, bus(&o_2b), bus(&o_2a), bus(&t_a1b1));
  stamp_add(ctx, bus_(BUFFER,2), bus(&o_2b), bus(&t_a2b0));
  // O3
  AiBj(bus(&t_a0b3), 0, 3);
  AiBj(bus(&t_a1b2), 1, 2);
  AiBj(bus(&t_a2b1), 2, 1);
  AiBj(bus(&t_a3b0), 3, 0);
  stamp_add(ctx, bus(&o_3a), bus(&t_a0b3), bus_(BUFFER, 3));
  stamp_add(ctx, bus(&o_3b), bus(&o_3a), bus(&t_a1b2));
  stamp_add(ctx, bus(&o_3c), bus(&o_3b), bus(&t_a2b1));
  stamp_add(ctx, bus_(BUFFER,3), bus(&o_3c), bus(&t_a3b0));
  ctx_emit(ctx, ret32, bus(BUFFER), 4);
}

// =================================================================================================
// SECTION: DIVISION
// =================================================================================================

static volatile _Alignas(1<<8) uint8_t CLZBU[1<<8];
static volatile _Alignas(1<<16) uint8_t CLZHU[1<<16];
static volatile _Alignas(1<<16) uint8_t SLL8B[1<<16], SLC8B[1<<16];
static volatile _Alignas(1<<16) uint8_t SRL8B[1<<16], SRC8B[1<<16];

static volatile _Alignas(1<<24) uint8_t QHAT[1<<24];
static volatile _Alignas(1<<24) uint8_t RHAT[1<<24];

void tab_init_div() {
  printf("\tinitializing CLZBU\n");
  for(int i=0;i<256;i++)
    CLZBU[i] = i?__builtin_clz(i)-24:8;
  printf("\tinitializing CLZHU\n");
  for(int i=0;i<256;i++)
    CLZHU[i] = i?__builtin_clz(i)-16:16;
  printf("\tinitializing SLx8B\n");
  for(int i=0;i<256;i++)
    for(int j=0;j<256;j++) {
      uint16_t shifter = i;
      shifter = j<=8?shifter<<j:0;
      SLL8B[i<<8|j] = shifter&0xff;
      SLC8B[i<<8|j] = shifter>>8;
    }
  printf("\tinitializing SRL8B\n");
  for(int i=0;i<256;i++)
    for(int j=0;j<256;j++) {
      uint16_t shifter = i << 8;
      shifter = j<=8?shifter>>j:0;
      SRL8B[i<<8|j] = shifter>>8;
      SRC8B[i<<8|j] = shifter&0xff;
    }
  printf("\tinitializing QHAT, RHAT\n");
  #pragma GCC unroll 128
  for(int d=128;d<(1<<8);d++)
    for(int n=0;n<(1<<16);n++) {
      QHAT[n<<8|d] = n / d;
      QHAT[(n<<8|d)-128] = (n / d) >> 8;
      RHAT[n<<8|d] = n % d;
    }
}

void mulhbuh(ctx_t *ctx, bus_t out16, bus_t lhs16, bus_t rhs8)
{
  // a0b0
  //      + a1b0
  static volatile uint16_t
    t_a0b0,        /*|*/
            t_a1b0 /*|*/ ;

  bus_t a0 = bus_add(lhs16,0), a1 = bus_add(lhs16,1);
  bus_t b0 = bus_add( rhs8,0);
  
  AiBj(bus(&t_a0b0), 0, 0);
  ctx_emit(ctx, bus_add(out16,0), bus_(&t_a0b0,0), 1);

  AiBj(bus(&t_a1b0), 1, 0);

  bus_t $0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld($0,+2,FLD_SRC,1), bus(&t_a1b0), 1);
  ctx_emit(ctx, bus_rel_fld($0,+2,FLD_SRC,2), bus_(&t_a0b0,1), 1);
  ctx_emit(ctx, bus_add(out16,1), bus_(TAB_SUPER,SUPER_ADD), 1);
}

void subhhuh(ctx_t *ctx, bus_t out16, bus_t lhs16, bus_t rhs16)
{
  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +2, FLD_SRC, 1), bus_add(rhs16,0), 1);
  ctx_emit(ctx, bus_rel_fld(_0, +2, FLD_SRC, 2), bus_add(lhs16,0), 1);
  ctx_emit(ctx, bus_add(out16,0), bus_(TAB_SUPER, SUPER_SUB), 2);

  staff_stamp_sub8_nocry(ctx, bus_add(out16,1), bus_add(lhs16,1), bus_add(rhs16,1));
}

void srlh8(ctx_t *ctx, bus_t out16, bus_t in16, bus_t shift8)
{
  bus_t $0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld($0,+2,FLD_SRC,0), shift8, 1);
  ctx_emit(ctx, bus_rel_fld($0,+2,FLD_SRC,1), bus_add(in16,1), 1);
  ctx_emit(ctx, bus_add(out16,1), bus(SRL8B), 1); // +2
  ctx_emit(ctx, bus_rel_fld($0,+5,FLD_SRC,0), shift8, 1);
  ctx_emit(ctx, bus_rel_fld($0,+5,FLD_SRC,1), bus_add(in16,1), 1);
  ctx_emit(ctx, bus_rel_fld($0,+9,FLD_SRC,1), bus(SRC8B), 1); // +5
  ctx_emit(ctx, bus_rel_fld($0,+8,FLD_SRC,0), shift8, 1);
  ctx_emit(ctx, bus_rel_fld($0,+8,FLD_SRC,1), bus_add(in16,0), 1);
  ctx_emit(ctx, bus_rel_fld($0,+9,FLD_SRC,0), bus(SRL8B), 1); // +8
  ctx_emit(ctx, bus_add(out16,0), bus(TAB_OR), 1); // +9
}

void sllh8(ctx_t *ctx, bus_t out16, bus_t in16, bus_t shift8)
{
  bus_t $0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld($0,+2,FLD_SRC,0), shift8, 1);
  ctx_emit(ctx, bus_rel_fld($0,+2,FLD_SRC,1), bus_add(in16,0), 1);
  ctx_emit(ctx, bus_add(out16,0), bus(SLL8B), 1); // +2
  ctx_emit(ctx, bus_rel_fld($0,+5,FLD_SRC,0), shift8, 1);
  ctx_emit(ctx, bus_rel_fld($0,+5,FLD_SRC,1), bus_add(in16,0), 1);
  ctx_emit(ctx, bus_rel_fld($0,+9,FLD_SRC,1), bus(SLC8B), 1); // +5
  ctx_emit(ctx, bus_rel_fld($0,+8,FLD_SRC,0), shift8, 1);
  ctx_emit(ctx, bus_rel_fld($0,+8,FLD_SRC,1), bus_add(in16,1), 1);
  ctx_emit(ctx, bus_rel_fld($0,+9,FLD_SRC,0), bus(SLL8B), 1); // +8
  ctx_emit(ctx, bus_add(out16,1), bus(TAB_OR), 1); // +9
}

void gtuhhub(ctx_t *ctx, bus_t ret8, bus_t lhs16, bus_t rhs16, int flags) {
  // lhs16 > rhs16 <=> !(lhs16 <= rhs16) <=> !(rhs16 >= lhs16)
  flags ^= CMP_INV;
  bus_t tmp;
  tmp = lhs16;
  lhs16 = rhs16;
  rhs16 = tmp;

  static volatile uint32_t rac_init = SUPER_GEU;
  ctx_emit(ctx, ret8, bus(&rac_init), 1);
  staff_stamp_cmp_bbb(ctx, bus_add(ret8, 0), bus_add(lhs16, 0), bus_add(rhs16, 0));
  staff_stamp_cmp_bbb(ctx, bus_add(ret8, 0), bus_add(lhs16, 1), bus_add(rhs16, 1));

  bus_t _0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld(_0, +1, FLD_SRC, 0), ret8, 1);
  ctx_emit(ctx, ret8,
           bus((flags & CMP_X4) ? ((flags & CMP_INV) ? TAB_ANTIPOL_TRUTH_4 : TAB_POL_TRUTH_4)
                                : ((flags & CMP_INV) ? TAB_ANTIPOL_TRUTH : TAB_POL_TRUTH)),
           1);
}


void
divhbuh(ctx_t *ctx, bus_t quot16, bus_t rem8, bus_t num16, bus_t den8)
{
  // guarantee: that den8 >= 128, so we do tight packing on the low 128

  bus_t $0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld($0,+2,FLD_SRC,1), num16, 2);
  ctx_emit(ctx, bus_rel_fld($0,+2,FLD_SRC,0), den8, 1);
  ctx_emit_strided(ctx, bus_add(quot16,0), bus(QHAT), 2, 1, _2d_src_stride(-129)); // +2
  // ctx_emit(ctx, bus_rel_fld($0,+5,FLD_SRC,0), bus_rel_fld($0,+3,FLD_SRC,0), 3);
  // ctx_emit(ctx, bus_add(quot16,1), bus(QHATHI), 1); // +5
  ctx_emit(ctx, bus_rel_fld($0,+4,FLD_SRC,0), bus_rel_fld($0,+2,FLD_SRC,0), 3);
  ctx_emit(ctx, rem8, bus(RHAT), 1); // +4
}

void
divhbub(ctx_t *ctx, bus_t quot8, bus_t rem8, bus_t numhi8, bus_t numlo8, bus_t den8)
{
  // guarantee: that den8 >= 128, so we do tight packing on the low 128
  bus_t $0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld($0,+3,FLD_SRC,2), numhi8, 1);
  ctx_emit(ctx, bus_rel_fld($0,+3,FLD_SRC,1), numlo8, 1);
  ctx_emit(ctx, bus_rel_fld($0,+3,FLD_SRC,0), den8, 1);
  ctx_emit(ctx, bus_add(quot8,0), bus(QHAT), 1); // +3
  ctx_emit(ctx, bus_rel_fld($0,+5,FLD_SRC,0), bus_rel_fld($0,+3,FLD_SRC,0), 3);
  ctx_emit(ctx, rem8, bus(RHAT), 1); // +5
}

void
clzbu(ctx_t *ctx, bus_t out8, bus_t in8)
{
  bus_t $0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld($0,+1,FLD_SRC,0), in8, 1);
  ctx_emit(ctx, out8, bus(CLZBU), 1);
}

void
clzhu(ctx_t *ctx, bus_t out8, bus_t in16)
{
  bus_t $0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld($0,+1,FLD_SRC,0), in16, 2);
  ctx_emit(ctx, out8, bus(CLZHU), 1);
}

struct {
  volatile uint8_t NUM[5], // 40-bit numerator (to account for SHIFT up to 8)
                   REM[4], // 32-bit partial remainders (plural!)
                   SHIFT,  // shift <8
                   DEN0;   // lowest byte of denominator
} static volatile DIVWBU;

void
divwbu(ctx_t* ctx, bus_t quot32, bus_t rem8, bus_t num32, bus_t den8)
{
  clzbu(ctx, bus(&DIVWBU.SHIFT), den8); // shift=clzbu_nz(den)

  bus_t $0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld($0,+2,FLD_SRC,1), den8, 1);
  ctx_emit(ctx, bus_rel_fld($0,+2,FLD_SRC,0), bus(&DIVWBU.SHIFT), 1);
  ctx_emit(ctx, bus(&DIVWBU.DEN0), bus(SLL8B), 1); // den0 = den << shift

  ctx_emit(ctx, bus_rel_fld($0,+5,FLD_SRC,1), bus_add(num32,3), 1);;
  ctx_emit(ctx, bus_rel_fld($0,+5,FLD_SRC,0), bus(&DIVWBU.SHIFT), 1);
  ctx_emit(ctx, bus(&DIVWBU.NUM[4]), bus(SLC8B), 1); // num4 = (nightmare)

  stamp_sll8(ctx, bus(DIVWBU.NUM), num32, bus(&DIVWBU.SHIFT)); // num[3210] = (num << shift) >> {24,16,8,0};

  divhbub(ctx, bus_add(quot32,3), bus(&DIVWBU.REM[3]), bus(&DIVWBU.NUM[4]), bus(&DIVWBU.NUM[3]), bus(&DIVWBU.DEN0));
  divhbub(ctx, bus_add(quot32,2), bus(&DIVWBU.REM[2]), bus(&DIVWBU.REM[3]), bus(&DIVWBU.NUM[2]), bus(&DIVWBU.DEN0));
  divhbub(ctx, bus_add(quot32,1), bus(&DIVWBU.REM[1]), bus(&DIVWBU.REM[2]), bus(&DIVWBU.NUM[1]), bus(&DIVWBU.DEN0));
  divhbub(ctx, bus_add(quot32,0), bus(&DIVWBU.REM[0]), bus(&DIVWBU.REM[1]), bus(&DIVWBU.NUM[0]), bus(&DIVWBU.DEN0));

  bus_t $1 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld($1,+2,FLD_SRC,1), bus(&DIVWBU.REM[0]), 1);
  ctx_emit(ctx, bus_rel_fld($1,+2,FLD_SRC,0), bus(&DIVWBU.SHIFT), 1);
  ctx_emit(ctx, rem8, bus(SRL8B), 1);
}

struct {
  volatile uint8_t NUM[5],
      DEN[2],
      SHIFT,
      RHAT[3];
  volatile uint16_t C1, C2, CDIFF, QHAT[3], REM[3], PROD;
} static volatile DIVWHU;

void
divwhu(ctx_t* ctx, bus_t quot24, bus_t rem16, bus_t num32, bus_t den16)
{
  clzbu(ctx, bus(&DIVWHU.SHIFT), bus_add(den16, 1)); // shift=clzbu_nz(den>>8)

  // bus_t $0 = ctx_label(ctx);
  // ctx_emit(ctx, bus_rel_fld($0,+2,FLD_SRC,1), bus_add(den16,0), 1);
  // ctx_emit(ctx, bus_rel_fld($0,+2,FLD_SRC,0), bus(&DIVWHU.SHIFT), 1);
  // ctx_emit(ctx, bus(&DIVWHU.DEN[0]), bus(SLL8B), 1);
  // ctx_emit(ctx, bus_rel_fld($0,+5,FLD_SRC,1), bus_add(den16,1), 1);
  // ctx_emit(ctx, bus_rel_fld($0,+5,FLD_SRC,0), bus(&DIVWHU.SHIFT), 1);
  // ctx_emit(ctx, bus(&DIVWHU.DEN[1]), bus(SLL8B), 1);
  // bus_t $1 = ctx_label(ctx);
  // ctx_emit(ctx, bus_rel_fld($1,+1,FLD_SRC,0), bus_rel_fld($0,+5,FLD_SRC,0), 2);
  // ctx_emit(ctx, bus_rel_fld($1,+3,FLD_SRC,0), bus(SLC8B), 1);
  // ctx_emit(ctx, bus_rel_fld($1,+3,FLD_SRC,1), bus(&DIVWHU.DEN[1]), 1);
  // ctx_emit(ctx, bus(&DIVWHU.DEN[1]), bus(TAB_OR), 1); // DEN[10] = den<<shift
  sllh8(ctx, bus(DIVWHU.DEN), den16, bus(&DIVWHU.SHIFT));

  bus_t $2 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld($2,+2,FLD_SRC,1), bus_add(num32,3), 1);
  ctx_emit(ctx, bus_rel_fld($2,+2,FLD_SRC,0), bus(&DIVWHU.SHIFT), 1);
  ctx_emit(ctx, bus(&DIVWHU.NUM[4]), bus(SLC8B), 1); // num4 = <oof>

  stamp_sll8(ctx, bus(DIVWHU.NUM), num32, bus(&DIVWHU.SHIFT)); // num[3210] = (num << shift) >> {24,16,8,0}
  
  static volatile _Alignas(256) uint8_t IDCS[8];
  for(int i=0;i<sizeof IDCS;i++) IDCS[i] = 0;
  IDCS[4|0] = 1;
  IDCS[4|1] = 2;

  printf("QHAT[7fff80]=%02hhx\n", QHAT[0x7fff80]);
  printf("QHAT[7fff00]=%02hhx\n", QHAT[0x7fff00]);

  // qhatN = divhbu(...)
  // c1 = qhatN * den0 (16x8)
  // c2 = (rhat2 << 8) | num2
  // idx = if c1 GT c2 { if c1 - c2 GT den16 then 2 else 1 } else 0
  // qN = qhatN - idx
  {
    divhbuh(ctx, bus(&DIVWHU.QHAT[2]), bus(&DIVWHU.RHAT[2]),
            bus(&DIVWHU.NUM[3]), bus(&DIVWHU.DEN[1])); // qhat2 = divhbu(num4, num3, den1, &rhat2)
    mulhbuh(ctx, bus(&DIVWHU.C1), bus(&DIVWHU.QHAT[2]), bus(&DIVWHU.DEN[0]));
    ctx_emit(ctx, bus_(&DIVWHU.C2,0), bus(&DIVWHU.NUM[2]), 1);
    cb_t *_br0 = ctx_here(ctx);
    ctx_emit(ctx, bus_(&DIVWHU.C2,1), bus(&DIVWHU.RHAT[2]), 1);
    
    bus_t $idx_load = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($idx_load,+2,FLD_SRC,1), bus(IDCS), 1); // rhs
    ctx_emit(ctx, bus_rel_fld($idx_load,+2,FLD_SRC,2), bus(&DIVWHU.QHAT[2]), 1); // lhs
    cb_t *_br1 = ctx_here(ctx);
    ctx_emit(ctx, bus_add(quot24,2), bus_(TAB_SUPER, SUPER_SUB), 1);

    bus_t $idx_or = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($idx_load,+0,FLD_SRC,0), bus(TAB_OR), 1);
    ctx_branch(ctx, $idx_load);

    _br0->NEXT_CB = ctx_label(ctx);
    gtuhhub(ctx, bus_rel_fld($idx_or,+0,FLD_SRC,0), bus(&DIVWHU.C1), bus(&DIVWHU.C2), CMP_X4);
    subhhuh(ctx, bus(&DIVWHU.CDIFF), bus(&DIVWHU.C1), bus(&DIVWHU.C2));
    gtuhhub(ctx, bus_rel_fld($idx_or,+0,FLD_SRC,1), bus(&DIVWHU.CDIFF), bus(&DIVWHU.DEN), 0);
    ctx_branch(ctx, $idx_or);
    _br1->NEXT_CB = ctx_label(ctx);
  }

  // rem = (num3 << 8 | num2) - q2 * den
  mulhbuh(ctx, bus(&DIVWHU.PROD), bus(DIVWHU.DEN), bus_add(quot24,2));
  subhhuh(ctx, bus(&DIVWHU.REM[2]), bus(&DIVWHU.NUM[2]), bus(&DIVWHU.PROD));

  {
    divhbuh(ctx, bus(&DIVWHU.QHAT[1]), bus(&DIVWHU.RHAT[1]),
            bus(&DIVWHU.REM[2]), bus(&DIVWHU.DEN[1])); // qhat1 = divhbu(rem>>8, rem, den1, &rhat2)
    mulhbuh(ctx, bus(&DIVWHU.C1), bus(&DIVWHU.QHAT[1]), bus(&DIVWHU.DEN[0]));
    ctx_emit(ctx, bus_(&DIVWHU.C2,0), bus(&DIVWHU.NUM[1]), 1);
    cb_t *_br0 = ctx_here(ctx);
    ctx_emit(ctx, bus_(&DIVWHU.C2,1), bus(&DIVWHU.RHAT[1]), 1);

    bus_t $idx_load = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($idx_load,+2,FLD_SRC,1), bus(IDCS), 1); // rhs
    ctx_emit(ctx, bus_rel_fld($idx_load,+2,FLD_SRC,2), bus(&DIVWHU.QHAT[1]), 1); // lhs
    cb_t *_br1 = ctx_here(ctx);
    ctx_emit(ctx, bus_add(quot24,1), bus_(TAB_SUPER, SUPER_SUB), 1);

    bus_t $idx_or = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($idx_load,+0,FLD_SRC,0), bus(TAB_OR), 1);
    ctx_branch(ctx, $idx_load);

    _br0->NEXT_CB = ctx_label(ctx);
    gtuhhub(ctx, bus_rel_fld($idx_or,+0,FLD_SRC,0), bus(&DIVWHU.C1), bus(&DIVWHU.C2), CMP_X4);
    subhhuh(ctx, bus(&DIVWHU.CDIFF), bus(&DIVWHU.C1), bus(&DIVWHU.C2));
    gtuhhub(ctx, bus_rel_fld($idx_or,+0,FLD_SRC,1), bus(&DIVWHU.CDIFF), bus(&DIVWHU.DEN), 0);
    ctx_branch(ctx, $idx_or);
    _br1->NEXT_CB = ctx_label(ctx);
  }
  
  // rem = (rem << 8 | num1) - q1 * den
  static volatile uint16_t TMP = 0;
  ctx_emit(ctx, bus_(&TMP,1), bus(&DIVWHU.REM[2]), 1);
  ctx_emit(ctx, bus_(&TMP,0), bus(&DIVWHU.NUM[1]), 1);
  mulhbuh(ctx, bus(&DIVWHU.PROD), bus(DIVWHU.DEN), bus_add(quot24,1));
  subhhuh(ctx, bus(&DIVWHU.REM[1]), bus(&TMP), bus(&DIVWHU.PROD));

  {
    divhbuh(ctx, bus(&DIVWHU.QHAT[0]), bus(&DIVWHU.RHAT[0]),
            bus(&DIVWHU.REM[1]), bus(&DIVWHU.DEN[1])); // qhat1 = divhbu(rem>>8, rem, den1, &rhat2)
    mulhbuh(ctx, bus(&DIVWHU.C1), bus(&DIVWHU.QHAT[0]), bus(&DIVWHU.DEN[0]));
    ctx_emit(ctx, bus_(&DIVWHU.C2,0), bus(&DIVWHU.NUM[0]), 1);
    cb_t *_br0 = ctx_here(ctx);
    ctx_emit(ctx, bus_(&DIVWHU.C2,1), bus(&DIVWHU.RHAT[0]), 1);

    bus_t $idx_load = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($idx_load,+2,FLD_SRC,1), bus(IDCS), 1); // rhs
    ctx_emit(ctx, bus_rel_fld($idx_load,+2,FLD_SRC,2), bus(&DIVWHU.QHAT[0]), 1); // lhs
    cb_t *_br1 = ctx_here(ctx);
    ctx_emit(ctx, bus_add(quot24,0), bus_(TAB_SUPER, SUPER_SUB), 1);

    bus_t $idx_or = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($idx_load,+0,FLD_SRC,0), bus(TAB_OR), 1);
    ctx_branch(ctx, $idx_load);

    _br0->NEXT_CB = ctx_label(ctx);
    gtuhhub(ctx, bus_rel_fld($idx_or,+0,FLD_SRC,0), bus(&DIVWHU.C1), bus(&DIVWHU.C2), CMP_X4);
    subhhuh(ctx, bus(&DIVWHU.CDIFF), bus(&DIVWHU.C1), bus(&DIVWHU.C2));
    gtuhhub(ctx, bus_rel_fld($idx_or,+0,FLD_SRC,1), bus(&DIVWHU.CDIFF), bus(&DIVWHU.DEN), 0);
    ctx_branch(ctx, $idx_or);
    _br1->NEXT_CB = ctx_label(ctx);
  }

  // rem[0] = (rem << 8 | num0) - q0 * den
  // rem_out = rem[0] >> shift
  ctx_emit(ctx, bus_(&TMP,1), bus(&DIVWHU.REM[1]), 1);
  ctx_emit(ctx, bus_(&TMP,0), bus(&DIVWHU.NUM[0]), 1);
  mulhbuh(ctx, bus(&DIVWHU.PROD), bus(DIVWHU.DEN), bus_add(quot24,0));
  subhhuh(ctx, bus(&DIVWHU.REM[0]), bus(&TMP), bus(&DIVWHU.PROD));

  srlh8(ctx, rem16, bus(&DIVWHU.REM[0]), bus(&DIVWHU.SHIFT));
}

  // volatile uint8_t NUM[5],
  //     DEN[2],
  //     SHIFT,
  //     RHAT[3];
  // volatile uint16_t C1, C2, CDIFF, QHAT[3], REM[3], PROD;

struct {
  volatile uint16_t NUM[3], DEN[2];
  volatile uint8_t SHIFT;
  volatile uint32_t C1, C2, CDIFF, PROD;
  volatile uint32_t QHAT;
  volatile uint16_t RHAT;
  volatile uint32_t REM;
} static volatile DIVU_WW;

void
divwwu(ctx_t* ctx, bus_t quot16, bus_t rem32, bus_t num32, bus_t den32)
{
  clzhu(ctx, bus(&DIVU_WW.SHIFT), bus_add(den32, 2)); // shift=clzhu(den>>16)
  stamp_sll(ctx, bus(&DIVU_WW.DEN), den32, bus(&DIVU_WW.SHIFT)); // den <<= shift

  // what i really need is a 32-to-48 widening SLL. this'll do though
  static volatile uint32_t TMP;
  ctx_emit(ctx, bus_(&TMP,2), bus(&K_ZERO), 2);
  ctx_emit(ctx, bus_(&TMP,0), bus_add(num32,2), 2);
  stamp_sll(ctx, bus(&DIVU_WW.NUM[1]), bus(&TMP), bus(&DIVU_WW.SHIFT));
  stamp_sll(ctx, bus(&DIVU_WW.NUM[0]), num32, bus(&DIVU_WW.SHIFT));

  // qhat0 = divwhu(num2,num1,den1,&rhat0);
  // c1 = qhat0 * den0
  // c2 = rhat0 * b + num0
  // idx = 0u16, 1u16, 2u16
  // q0 = subhhuh(qhat0, idx)
  // idx = if c1 GT c2 { if c1 - c2 GT den32 then 2u16 else 1u16 } else 0u16
  divwhu(ctx, bus(&DIVU_WW.QHAT), bus(&DIVU_WW.RHAT), bus(&DIVU_WW.NUM[1]), bus(&DIVU_WW.DEN[1]));

  // strictly, what we need here is a 32x16->32 multiply, but 32x32->32 will suffice
  static volatile uint32_t MULTMP = 0;
  ctx_emit(ctx, bus_(&TMP,0), bus(&DIVU_WW.DEN[0]), 2);
  stamp_umul_www(ctx, bus(&DIVU_WW.C1), bus(&DIVU_WW.QHAT), bus(&TMP));

  ctx_emit(ctx, bus_(&DIVU_WW.C2,0), bus(&DIVU_WW.NUM[0]), 2);
  cb_t *_br0 = ctx_here(ctx);
  ctx_emit(ctx, bus_(&DIVU_WW.C2,2), bus(&DIVU_WW.RHAT), 2);

  static volatile _Alignas(256) uint8_t IDCS[8];
  for(int i=0;i<sizeof IDCS;i++) IDCS[i] = 0;
  IDCS[4|0] = 1;
  IDCS[4|1] = 2;

  static volatile uint16_t SUBTRAHEND = 0;
  bus_t $idx_load = ctx_label(ctx);
  ctx_emit(ctx, bus(&SUBTRAHEND), bus(IDCS), 1);
  subhhuh(ctx, quot16, bus(&DIVU_WW.QHAT), bus(&SUBTRAHEND));
  cb_t *_br1 = ctx_last(ctx);

  bus_t $idx_or = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld($idx_load,+0,FLD_SRC,0), bus(TAB_OR), 1);
  ctx_branch(ctx, $idx_load);

  _br0->NEXT_CB = ctx_label(ctx);
  // c1>c2 <=> !(c2>=c1)
  stamp_geu_bww(ctx, bus_rel_fld($idx_or,+0,FLD_SRC,0), bus(&DIVU_WW.C2), bus(&DIVU_WW.C1), CMP_X4|CMP_INV);
  stamp_sub(ctx, bus(&DIVU_WW.CDIFF), bus(&DIVU_WW.C1), bus(&DIVU_WW.C2));
  stamp_geu_bww(ctx, bus_rel_fld($idx_or,+0,FLD_SRC,1), bus(DIVU_WW.DEN), bus(&DIVU_WW.CDIFF), CMP_INV);
  ctx_branch(ctx, $idx_or);
  _br1->NEXT_CB = ctx_label(ctx);

  // REM = NUM - q0_16 * DEN
  // rem32 = REM >> shift
  // note, however, that this is equivalent to
  // rem32 = num32 - q0_16 * den32

  // again, what's actually needed is 32x16->32 multiply
  ctx_emit(ctx, bus_(&TMP,0), quot16, 2);
  stamp_umul_www(ctx, bus(&DIVU_WW.PROD), bus(&TMP), den32);
  stamp_sub(ctx, rem32, num32, bus(&DIVU_WW.PROD));
}

enum {
  WHICH_0000 = 128,
  WHICH_000x = 129,
  WHICH_00xx = 130,
  WHICH_xxxx = 131,
};
static volatile uint8_t WHICH = 0;
#define SET_WHICH(which) do{static volatile uint8_t _WHICH=WHICH_##which;ctx_emit(ctx,bus(&WHICH),bus(&_WHICH),1);} while(0)

typedef struct {
  volatile _Alignas(1 << 8) uint32_t IBR[4];
  volatile _Alignas(1 << 8) uint32_t SBR[4];
} udivmod_branches_t;

void
udivmod_exits_set_next(udivmod_exits_t exits, bus_t next)
{
  exits.w0u->NEXT_CB = next;
  exits.wbu->NEXT_CB = next;
  exits.whu->NEXT_CB = next;
  exits.wwu->NEXT_CB = next;
}

static udivmod_branches_t *
udivmod_branches_alloc(void)
{
  udivmod_branches_t *branches = kmalloc_aligned(sizeof *branches, 1 << 8);
  assert(branches);
  assert(((uintptr_t)branches & ((1 << 8) - 1)) == 0);
  __builtin_memset((void *)branches, 0, sizeof *branches);
  return branches;
}

udivmod_exits_t
stamp_udivmod_www(ctx_t* ctx, bus_t quot32, bus_t rem32, bus_t num32, bus_t den32)
{
  static volatile _Alignas(1 << 16) uint8_t EQUZHB4[1 << 16];
  //       HI=0 HI!=0
  // LO=0  0    4
  // LO!=0 8    12
  for (int i = 0; i < (1 << 16); i++)
    EQUZHB4[i] = ((i & 0x00ff) ? +8 : +0) + ((i & 0xff00) ? +4 : +0);
  udivmod_branches_t *branches = udivmod_branches_alloc();

  bus_t $0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld($0, +1, FLD_SRC, 0), bus_add(den32, 2), 2);
  ctx_emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 0), bus(EQUZHB4), 1);
  ctx_emit(ctx, bus_rel_fld($0, +3, FLD_NXT, 0), bus(branches->IBR), 4);
  ctx_emit(ctx, bus(&K_ZERO), bus(&K_ZERO), 4);

  branches->IBR[0] = ctx_label(ctx)._0; // (den>>16) == 0 -> divwhu or divwbu

  cb_t *_end_whu_branch, *_end_wbu_branch, *_end_w0u_branch;
  {
    bus_t $1 = ctx_label(ctx);
    ctx_emit(ctx, bus_rel_fld($1, +1, FLD_SRC, 0), bus_add(den32, 0), 2);
    ctx_emit(ctx, bus_rel_fld($1, +2, FLD_SRC, 0), bus(EQUZHB4), 1);
    ctx_emit(ctx, bus_rel_fld($1, +3, FLD_NXT, 0), bus(branches->SBR), 4);
    ctx_emit(ctx, bus(&K_ZERO), bus(&K_ZERO), 4);

    branches->SBR[0] = ctx_label(ctx)._0; // den=[0,0,0,0]
    SET_WHICH(0000);
    static volatile uint32_t K_NEG1 = 0xffffffff;
    ctx_emit(ctx, quot32, bus(&K_NEG1), 4);
    ctx_emit(ctx, rem32, num32, 4);
    _end_w0u_branch = ctx_last(ctx);

    branches->SBR[2] = ctx_label(ctx)._0; // den=[0,0,0,den0]
    SET_WHICH(000x);
    divwbu(ctx, quot32, rem32 /*as 8*/, num32, den32 /*as 8*/);
    ctx_emit(ctx, bus_add(rem32,1), bus(&K_ZERO), 3);
    _end_wbu_branch = ctx_last(ctx);

    branches->SBR[1] = branches->SBR[3] = ctx_label(ctx)._0; // den=[0,0,den1,?den0]
    SET_WHICH(00xx);
    divwhu(ctx, quot32 /*as 24*/, rem32 /*as 16*/, num32, den32 /*as 16*/);
    ctx_emit(ctx, bus_add(rem32,2), bus(&K_ZERO), 2);
    ctx_emit(ctx, bus_add(quot32,3), bus(&K_ZERO), 1);
    _end_whu_branch = ctx_last(ctx);
  }

  branches->IBR[1] = branches->IBR[2] = branches->IBR[3] = ctx_label(ctx)._0; // (den>>16) != 0 -> divwwu
  SET_WHICH(xxxx);
  divwwu(ctx, quot32 /*as 16*/, rem32, num32, den32);
  ctx_emit(ctx, bus_add(quot32, 2), bus(&K_ZERO), 2);

  return (udivmod_exits_t){
    .w0u = _end_w0u_branch,
    .wbu = _end_wbu_branch,
    .whu = _end_whu_branch,
    .wwu = ctx_last(ctx),
  };
}

static volatile uint32_t X_A = 0, X_B = 0, B_Z = 0, A_U, B_U;

typedef struct {
  volatile _Alignas(256) uint32_t A_ABS[2];
  volatile _Alignas(256) uint32_t B_ABS[2];
  volatile _Alignas(1 << 16) uint32_t QUOT_SIGN_TABLE[1 << 16];
  volatile _Alignas(256) uint32_t QUOT_ZERO_TABLE[2];
  volatile _Alignas(256) uint32_t REM_SIGN_TABLE[2];
} divmod_tables_t;

static divmod_tables_t *
divmod_tables_alloc(void)
{
  divmod_tables_t *tables = kmalloc_aligned(sizeof *tables, 1 << 16);
  assert(tables);
  assert(((uintptr_t)tables & ((1 << 16) - 1)) == 0);
  __builtin_memset((void *)tables, 0, sizeof *tables);
  return tables;
}

void
stamp_divmod_www(ctx_t *ctx,
                 bus_t ret32,
                 bus_t rem32,
                 bus_t a32,
                 bus_t b32)
{
  static volatile _Alignas(256) uint8_t CMPZ_TABLE[256];
  for(int i=0;i<256;i++)
    CMPZ_TABLE[i] = (i&0x80)?4:0;
  divmod_tables_t *tables = divmod_tables_alloc();

  bus_t $0 = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld($0,+1,FLD_SRC,0), bus_add(a32, 3), 1);
  ctx_emit(ctx, bus(&X_A), bus(CMPZ_TABLE), 1);
  ctx_emit(ctx, bus_rel_fld($0,+3,FLD_SRC,0), bus_add(b32, 3), 1);
  ctx_emit(ctx, bus(&X_B), bus(CMPZ_TABLE), 1);
  staff_stamp_eq_bww(ctx, bus(&B_Z), b32, bus(&K_ZERO), CMP_X4);

  bus_t $load_a_abs = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld($load_a_abs,+1,FLD_SRC,0), bus(&X_A), 1);
  ctx_emit(ctx, bus_rel_fld($load_a_abs,+2,FLD_NXT,0), bus(tables->A_ABS), 4);
  cb_t *_xa_to_sign_table = ctx_here(ctx);
  ctx_emit(ctx, bus(&K_ZERO), bus(&X_A), 1);

  tables->A_ABS[1] = ctx_label(ctx)._0;
  stamp_neg(ctx, bus(&A_U), a32);
  ctx_branch(ctx, bus_rel_cb(ctx_label(ctx),+1));
  tables->A_ABS[0] = ctx_label(ctx)._0;
  ctx_emit(ctx, bus(&A_U), a32, 4);

  bus_t $load_b_abs = ctx_label(ctx);
  ctx_emit(ctx, bus_rel_fld($load_b_abs,+1,FLD_SRC,0), bus(&X_B), 1);
  ctx_emit(ctx, bus_rel_fld($load_b_abs,+2,FLD_NXT,0), bus(tables->B_ABS), 4);
  cb_t *_xb_to_sign_table = ctx_here(ctx);
  ctx_emit(ctx, bus(&K_ZERO), bus(&X_B), 1);

  tables->B_ABS[1] = ctx_label(ctx)._0;
  stamp_neg(ctx, bus(&B_U), b32);
  ctx_branch(ctx, bus_rel_cb(ctx_label(ctx),+1));
  tables->B_ABS[0] = ctx_label(ctx)._0;
  ctx_emit(ctx, bus(&B_U), b32, 4);

  cb_t *_xa_to_rem_sign_table = ctx_here(ctx);
  ctx_emit(ctx, bus(&K_ZERO), bus(&X_A), 1);
  cb_t *_bz_to_qzero_table = ctx_here(ctx);
  ctx_emit(ctx, bus(&K_ZERO), bus(&B_Z), 1);
  udivmod_exits_t udiv_exits = stamp_udivmod_www(ctx, ret32, rem32, bus(&A_U), bus(&B_U));

  bus_t $qzero_dispatch = ctx_label(ctx);
  udivmod_exits_set_next(udiv_exits, $qzero_dispatch);
  _bz_to_qzero_table->DST_ADDR = bus_rel_fld($qzero_dispatch,+0,FLD_SRC,0);
  ctx_emit(ctx, bus_rel_fld($qzero_dispatch,+1,FLD_NXT,0), bus(tables->QUOT_ZERO_TABLE), 4);
  ctx_emit(ctx, bus(&K_ZERO), bus(&K_ZERO), 4);

  bus_t $quot_dispatch = ctx_label(ctx);
  tables->QUOT_ZERO_TABLE[0] = $quot_dispatch._0;
  _xa_to_sign_table->DST_ADDR = bus_rel_fld($quot_dispatch,+0,FLD_SRC,0);
  _xb_to_sign_table->DST_ADDR = bus_rel_fld($quot_dispatch,+0,FLD_SRC,1);
  ctx_emit(ctx, bus_rel_fld($quot_dispatch,+1,FLD_NXT,0), bus(tables->QUOT_SIGN_TABLE), 4);
  ctx_emit(ctx, bus(&K_ZERO), bus(&K_ZERO), 4);

  bus_t $qneg = ctx_label(ctx);
  tables->QUOT_SIGN_TABLE[0x100] = tables->QUOT_SIGN_TABLE[0x001] = $qneg._0;
  stamp_neg(ctx, ret32, ret32);

  bus_t $rem_dispatch = ctx_label(ctx);
  tables->QUOT_SIGN_TABLE[0x000] = tables->QUOT_SIGN_TABLE[0x101] = $rem_dispatch._0;
  tables->QUOT_ZERO_TABLE[1] = $rem_dispatch._0;
  _xa_to_rem_sign_table->DST_ADDR = bus_rel_fld($rem_dispatch,+0,FLD_SRC,0);
  ctx_emit(ctx, bus_rel_fld($rem_dispatch,+1,FLD_NXT,0), bus(tables->REM_SIGN_TABLE), 4);
  ctx_emit(ctx, bus(&K_ZERO), bus(&K_ZERO), 4);

  bus_t $rneg = ctx_label(ctx);
  tables->REM_SIGN_TABLE[1] = $rneg._0;
  stamp_neg(ctx, rem32, rem32);

  tables->REM_SIGN_TABLE[0] = ctx_label(ctx)._0;
  ctx_emit(ctx, bus(&K_ZERO), bus(&K_ZERO), 4);
}

// =================================================================================================
// SECTION: INITIALIZER LISTS
// =================================================================================================

static void (*tab_inits[])() = {tab_init_inc, tab_init_sub, tab_init_add, tab_init_not, tab_init_and,
                         tab_init_or, tab_init_xor, tab_init_sll, tab_init_srl, tab_init_sra,
                         tab_init_ge, tab_init_sext, tab_init_mul, tab_init_div };

void staff_init_all_stamp_tables() {
  const int n_inits = sizeof tab_inits / sizeof *tab_inits;
  for (int i = 0; i < n_inits; i++) {
    (tab_inits[i])();
  }
}

#endif
