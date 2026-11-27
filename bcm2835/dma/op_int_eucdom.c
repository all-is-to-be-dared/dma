#include <bcm2835/dma/arith.h>

// Operations on the Euclidean Domain of ZZ_32.

// -------------------------------------------------------------------------------------------------
// DIVISION
//
// Basic strategy: implement  32/8, 32/16, 32/32, then make a multiplexer, then add sign support on
//                 top.

volatile uint8_t *QHAT;
volatile uint8_t *RHAT;

INITIALIZER(div_core)
{
#pragma GCC unroll 128
  for (int d = 128; d < (1 << 8); d++)
    for (int n = 0; n < (1 << 16); n++) {
      QHAT[n << 8 | d] = n / d;
      QHAT[(n << 8 | d) - 128] = (n / d) >> 8;
      RHAT[n << 8 | d] = n % d;
    }
}

INITIALIZER(div_sxx8b)
{
  for (int i = 0; i < 256; i++)
    for (int j = 0; j < 256; j++) {
      uint16_t shifter = i;
      shifter = (j <= 8) ? shifter << j : 0;
      HYPER[i << 16 | j << 8 | HYPER_SLL8B] = shifter & 0xff;
      HYPER[i << 16 | j << 8 | HYPER_SLC8B] = shifter >> 8;
    }
  for (int i = 0; i < 256; i++)
    for (int j = 0; j < 256; j++) {
      uint16_t shifter = i << 8;
      shifter = (j <= 8) ? shifter >> j : 0;
      HYPER[i << 16 | j << 8 | HYPER_SRL8B] = shifter >> 8;
      HYPER[i << 16 | j << 8 | HYPER_SRC8B] = shifter & 0xff;
    }
}

INITIALIZER(div_clzxu)
{
  for (int i = 0; i < 256; i++)
    SUPER[i << 8 | SUPER_CLZBU] = i ? __builtin_clz(i) - 24 : 8;
  for (int i = 0; i < (1 << 16); i++)
    HYPER[i << 8 | HYPER_CLZHU] = i ? __builtin_clz(i) - 16 : 16;
}

struct
{
  volatile uint8_t alignas(4) NUM[5];
  volatile uint8_t alignas(4) REM[4];
  volatile uint8_t SHIFT, DEN0;
} static volatile State_32_8;

void
Div_32_8(emit_ctx *ctx, busad quot32, busad rem8, busad num32, busad den8)
{
  // Probe("32_8.NUM", State_32_8.NUM, 5);
  // Probe("32_8.REM", State_32_8.REM, 4);
  // Probe("32_8.SHIFT", &State_32_8.SHIFT, 1);
  // Probe("32_8.DEN0", &State_32_8.DEN0, 1);

  // CountLeadingZeros_8(ctx, bus(&State_32_8.SHIFT), den8);
  busad $_clz8 = Label(ctx);
  Emit(ctx, bus_rel_fld($_clz8, +1, FLD_SRC, 1), den8, 1);
  Emit(ctx, bus(&State_32_8.SHIFT), bus_(SUPER, SUPER_CLZBU), 1);

  busad $0 = Label(ctx);
  Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), den8, 1);
  Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), bus(&State_32_8.SHIFT), 1);
  Emit(ctx, bus(&State_32_8.DEN0), bus_(HYPER, HYPER_SLL8B), 1); // den0 = den << shift

  Emit(ctx, bus_rel_fld($0, +5, FLD_SRC, 2), bus_add(num32, 3), 1);
  Emit(ctx, bus_rel_fld($0, +5, FLD_SRC, 1), bus(&State_32_8.SHIFT), 1);
  Emit(ctx, bus(&State_32_8.NUM[4]), bus_(HYPER, HYPER_SLC8B), 1); // num4 = (nightmare)

  Sll8(ctx,
       4,
       bus(State_32_8.NUM),
       num32,
       bus(&State_32_8.SHIFT)); // num[3210] = (num << shift) >> {24,16,8,0};

  {
    // --- divhbub(ctx, q8, r8, numhi8, numlo8, den8)
    // divhbub(ctx, bus_add(quot32,3), bus(&State_32_8.REM[3]), bus(&State_32_8.NUM[4]),
    // bus(&State_32_8.NUM[3]), bus(&State_32_8.DEN0)); divhbub(ctx, bus_add(quot32,2),
    // bus(&State_32_8.REM[2]), bus(&State_32_8.REM[3]), bus(&State_32_8.NUM[2]),
    // bus(&State_32_8.DEN0)); divhbub(ctx, bus_add(quot32,1), bus(&State_32_8.REM[1]),
    // bus(&State_32_8.REM[2]), bus(&State_32_8.NUM[1]), bus(&State_32_8.DEN0)); divhbub(ctx,
    // bus_add(quot32,0), bus(&State_32_8.REM[0]), bus(&State_32_8.REM[1]), bus(&State_32_8.NUM[0]),
    // bus(&State_32_8.DEN0));

    busad $10 = Label(ctx);
    Emit(ctx, bus_rel_fld($10, +2, FLD_SRC, 1), bus(&State_32_8.NUM[3]), 2);
    Emit(ctx, bus_rel_fld($10, +2, FLD_SRC, 0), bus(&State_32_8.DEN0), 1);
    Emit(ctx, bus_add(quot32, 3), bus(QHAT), 1);
    Emit(ctx, bus_rel_fld($10, +4, FLD_SRC, 0), bus_rel_fld($10, +2, FLD_SRC, 0), 3);
    Emit(ctx, bus(&State_32_8.REM[3]), bus(RHAT), 1);

    busad $11 = Label(ctx);
    EmitStrided(ctx, bus_rel_fld($11, +3, FLD_SRC, 1), bus(&State_32_8.NUM[2]), 3, 1, +127, -2);
    EmitStridedWith(ctx,
                    TI_DST_INC | TI_2DMODE,
                    bus_rel_fld($11, +3, FLD_SRC, 0),
                    bus(&State_32_8.DEN0),
                    3,
                    1,
                    +127,
                    0);
    for (int i = 3; i > 0; i--) {
      busad $12 = Label(ctx);
      Emit(ctx, bus_rel_fld($12, +1, FLD_SRC, 2), bus(&State_32_8.REM[i]), 1);
      Emit(ctx, bus_add(quot32, i - 1), bus(QHAT), 1);
      // Probe("32_8.*QH1_3", &Last(ctx)->src_addr, 4);
      Emit(ctx, bus_rel_fld($12, +3, FLD_SRC, 0), bus_rel_fld($12, +1, FLD_SRC, 0), 3);
      Emit(ctx, bus(&State_32_8.REM[i - 1]), bus(RHAT), 1);
      // Probe("32_8.*RH1_3", &Last(ctx)->src_addr, 4);
    }
  }

  // Is this correct or is a wider SRL needed?
  busad $20 = Label(ctx);
  Emit(ctx, bus_rel_fld($20, +2, FLD_SRC, 2), bus(&State_32_8.REM[0]), 1);
  Emit(ctx, bus_rel_fld($20, +2, FLD_SRC, 1), bus(&State_32_8.SHIFT), 1);
  Emit(ctx, rem8, bus_(HYPER, HYPER_SRL8B), 1);
}

struct
{
  volatile uint8_t NUM[5], DEN[2], SHIFT, RHAT[3];
  volatile uint16_t C1, C2, CDIFF, QHAT[3], REM[3], PROD;
} static volatile State_32_16;

// INVARIANT: (den16>>8) != 0
void
Div_32_16(emit_ctx *ctx, busad quot24, busad rem16, busad num32, busad den16)
{
  // CountLeadingZeros_8(ctx, bus(&State_32_16.SHIFT), bus_add(den16, 1));
  busad $_clz8 = Label(ctx);
  Emit(ctx, bus_rel_fld($_clz8, +1, FLD_SRC, 1), bus_add(den16, 1), 1);
  Emit(ctx, bus(&State_32_16.SHIFT), bus_(SUPER, SUPER_CLZBU), 1);

  Sll8(ctx, 2, bus(&State_32_16.DEN), den16, bus(&State_32_16.SHIFT));

  busad $oof = Label(ctx);
  Emit(ctx, bus_rel_fld($oof, +2, FLD_SRC, 2), bus_add(num32, 3), 1);
  Emit(ctx, bus_rel_fld($oof, +2, FLD_SRC, 1), bus(&State_32_16.SHIFT), 1);
  Emit(ctx, bus(&State_32_16.NUM[4]), bus_(HYPER, HYPER_SLC8B), 1); // num4 = <oof>

  Sll8(ctx,
       4,
       bus(State_32_16.NUM),
       num32,
       bus(&State_32_16.SHIFT)); // num[3210] = (num << shift) >> {24,16,8,0}

  uint8_t *IDCS = MonomorphizeOn(0, arena_alloc(&ARENA, 8, 256));
  for (int i = 0; i < 8; i++)
    IDCS[i] = 0;
  IDCS[4 | 0] = 1;
  IDCS[4 | 1] = 2;

  // ROUTINE divhbuh(ctx, quot16, rem8, num16, den8)
  //   bus_t $0 = ctx_label(ctx);
  //   ctx_emit(ctx, bus_rel_fld($0,+2,FLD_SRC,1), num16, 2);
  //   ctx_emit(ctx, bus_rel_fld($0,+2,FLD_SRC,0), den8, 1);
  //   ctx_emit_strided(ctx, bus_add(quot16,0), bus(QHAT), 2, 1, _2d_src_stride(-129)); // +2
  //   ctx_emit(ctx, bus_rel_fld($0,+4,FLD_SRC,0), bus_rel_fld($0,+2,FLD_SRC,0), 3);
  //   ctx_emit(ctx, rem8, bus(RHAT), 1); // +4
  //
  // ROUTINE mulhbuh(ctx, out16, lhs16, rhs8)
  // ROUTINE subhhuh(ctx, out16, lhs16, rhs16)

  for (int j = 0; j < 3; j++) {
    int i = 2 - j;
    {
      // divhbuh(ctx, bus(&State_32_16.QHAT[2]), bus(&State_32_16.RHAT[2]),
      // bus(&State_32_16.NUM[3]), bus(&State_32_16.DEN[1])); // qhat2 = divhbu(num4, num3, den1,
      // &rhat2)
      busad $_d0 = Label(ctx);
      volatile void *num = i == 2 ? (void *)&State_32_16.NUM[i + 1] : &State_32_16.REM[i + 1];
      Emit(ctx, bus_rel_fld($_d0, +2, FLD_SRC, 1), bus(num), 2);
      Emit(ctx, bus_rel_fld($_d0, +2, FLD_SRC, 0), bus(&State_32_16.DEN[1]), 1);
      EmitStrided(ctx, bus(&State_32_16.QHAT[i]), bus(QHAT), 2, 1, -129, 0); // +2
      Emit(ctx, bus_rel_fld($_d0, +4, FLD_SRC, 0), bus_rel_fld($_d0, +2, FLD_SRC, 0), 3);
      Emit(ctx, bus(&State_32_16.RHAT[i]), bus(RHAT), 1); // +4

      // mulhbuh(ctx, bus(&State_32_16.C1), bus(&State_32_16.QHAT[2]), bus(&State_32_16.DEN[0]));
      {
        busad a0 = bus_(&State_32_16.QHAT[i], 0), a1 = bus_(&State_32_16.QHAT[i], 1);
        busad b0 = bus(&State_32_16.DEN[0]);

        busad $a0b0 = Label(ctx);
        Emit(ctx, bus_rel_fld($a0b0, +2, FLD_SRC, 1), a0, 1);
        Emit(ctx, bus_rel_fld($a0b0, +2, FLD_SRC, 2), b0, 1);
        Emit(ctx, bus_(&State_32_16.C1, 0), bus_(HYPER, HYPER_MUL), 2);

        busad $a1b0 = Label(ctx);
        Emit(ctx, bus_rel_fld($a1b0, +2, FLD_SRC, 1), a1, 1);
        Emit(ctx, bus_rel_fld($a1b0, +2, FLD_SRC, 2), b0, 1);
        Emit(ctx, bus_rel_fld($a1b0, +4, FLD_SRC, 1), bus_(HYPER, HYPER_MUL), 1);
        Emit(ctx, bus_rel_fld($a1b0, +4, FLD_SRC, 2), bus_(&State_32_16.C1, 1), 1);
        Emit(ctx, bus_(&State_32_16.C1, 1), bus_(HYPER, HYPER_ADD), 1);
      }

      Emit(ctx, bus_(&State_32_16.C2, 0), bus(&State_32_16.NUM[i]), 1);
      cblk *_br0 = Emit(ctx, bus_(&State_32_16.C2, 1), bus(&State_32_16.RHAT[i]), 1);

      busad $idx_load = Label(ctx);
      Emit(ctx, bus_rel_fld($idx_load, +2, FLD_SRC, 1), bus(IDCS), 1);                 // rhs
      Emit(ctx, bus_rel_fld($idx_load, +2, FLD_SRC, 2), bus(&State_32_16.QHAT[i]), 1); // lhs
      cblk *_br1 = Emit(ctx, bus_add(quot24, i), bus_(HYPER, HYPER_SUB), 1);

      busad $idx_or = Label(ctx);
      Emit(ctx, bus_rel_fld($idx_load, +0, FLD_SRC, 0), bus_(HYPER, HYPER_OR), 1);
      Branch(ctx, $idx_load);

      _br0->next_cb = Label(ctx);
      // a>b is !(a<=b) is !(b>=a)
      // gtuhhub(ctx, bus_rel_fld($idx_or,+0,FLD_SRC,1), bus(&State_32_16.C1), bus(&State_32_16.C2),
      // ICF_X4);
      Geu(ctx,
          2,
          bus_rel_fld($idx_or, +0, FLD_SRC, 1),
          bus(&State_32_16.C2),
          bus(&State_32_16.C1),
          ICF_X4 | ICF_INV);
      Sub(ctx, 2, bus(&State_32_16.CDIFF), bus(&State_32_16.C1), bus(&State_32_16.C2));
      // gtuhhub(ctx, bus_rel_fld($idx_or,+0,FLD_SRC,1), bus(&DIVWHU.CDIFF), bus(&DIVWHU.DEN), 0);
      Geu(ctx,
          2,
          bus_rel_fld($idx_or, +0, FLD_SRC, 2),
          bus(&State_32_16.DEN),
          bus(&State_32_16.CDIFF),
          ICF_INV);
      Branch(ctx, $idx_or);
      _br1->next_cb = Label(ctx);
    }

    uint16_t *TMP = MonomorphizeOn(0, arena_alloc(&ARENA, 2, 4));

    // mulhbuh(ctx, bus(&State_32_16.PROD), bus(State_32_16.DEN), bus_add(quot24,i));
    {
      busad a0 = bus_(&State_32_16.DEN, 0), a1 = bus_(&State_32_16.DEN, 1);
      busad b0 = bus_add(quot24, i);

      busad $a0b0 = Label(ctx);
      Emit(ctx, bus_rel_fld($a0b0, +2, FLD_SRC, 1), a0, 1);
      Emit(ctx, bus_rel_fld($a0b0, +2, FLD_SRC, 2), b0, 1);
      Emit(ctx, bus_(&State_32_16.PROD, 0), bus_(HYPER, HYPER_MUL), 2);

      busad $a1b0 = Label(ctx);
      Emit(ctx, bus_rel_fld($a1b0, +2, FLD_SRC, 1), a1, 1);
      Emit(ctx, bus_rel_fld($a1b0, +2, FLD_SRC, 2), b0, 1);
      Emit(ctx, bus_rel_fld($a1b0, +4, FLD_SRC, 1), bus_(HYPER, HYPER_MUL), 1);
      Emit(ctx, bus_rel_fld($a1b0, +4, FLD_SRC, 2), bus_(&State_32_16.PROD, 1), 1);
      Emit(ctx, bus_(&State_32_16.PROD, 1), bus_(HYPER, HYPER_ADD), 1);
    }

    if (i == 2) {
      Sub(ctx, 2, bus(&State_32_16.REM[2]), bus(&State_32_16.NUM[2]), bus(&State_32_16.PROD));
    } else if (i == 1) {
      Emit(ctx, bus_(TMP, 1), bus(&State_32_16.REM[2]), 1);
      Emit(ctx, bus_(TMP, 0), bus(&State_32_16.NUM[1]), 1);
      Sub(ctx, 2, bus(&State_32_16.REM[1]), bus(TMP), bus(&State_32_16.PROD));
    } else if (i == 0) {
      Emit(ctx, bus_(TMP, 1), bus(&State_32_16.REM[1]), 1);
      Emit(ctx, bus_(TMP, 0), bus(&State_32_16.NUM[0]), 1);
      Sub(ctx, 2, bus(&State_32_16.REM[0]), bus(TMP), bus(&State_32_16.PROD));

      Srl8(ctx, 2, rem16, bus(&State_32_16.REM[0]), bus(&State_32_16.SHIFT));
    }
  }
}

struct
{
  volatile uint16_t alignas(4) NUM[3];
  volatile uint16_t alignas(4) DEN[2];
  // uint64_t PAD[16];
  volatile uint8_t alignas(4) SHIFT;
  volatile uint32_t C1, C2, CDIFF, PROD;
  volatile uint32_t QHAT;
  volatile uint16_t alignas(4) RHAT;
  volatile uint32_t REM;
} static volatile State_32_32;

// INVARIANT: (den32>>16) != 0
void
Div_32_32(emit_ctx *ctx, busad quot16, busad rem32, busad num32, busad den32)
{
  // Probe("32_32.NUM", State_32_32.NUM, 6);
  // Probe("32_32.DEN", State_32_32.DEN, 4);
  // Probe("32_32.SHIFT", &State_32_32.SHIFT, 1);
  // Probe("32_32.C1", &State_32_32.C1, 4);
  // Probe("32_32.C2", &State_32_32.C2, 4);
  // Probe("32_32.CDIFF", &State_32_32.CDIFF, 4);
  // Probe("32_32.PROD", &State_32_32.PROD, 4);
  // Probe("32_32.QHAT", &State_32_32.QHAT, 4);
  // Probe("32_32.RHAT", &State_32_32.RHAT, 2);
  // Probe("32_32.REM", &State_32_32.REM, 4);

  // Count leading zeros in the upper halfword of DEN32
  // This is <16, since we are guaranteed that the upper halfword is nonzero.
  // CountLeadingZeros_16High(ctx, bus(&State_32_32.SHIFT), bus_add(den32, 2)); //
  // shift=clzhu(den>>16)
  busad $_clz16 = Label(ctx);
  Emit(ctx, bus_rel_fld($_clz16, +1, FLD_SRC, 1), bus_add(den32, 2), 2);
  Emit(ctx, bus(&State_32_32.SHIFT), bus_(HYPER, HYPER_CLZHU), 1);
  // Probe("32_32.ShiftLoader", &Last(ctx)->src_addr, 4);

  // Shift the denominator so that the highest bit is set
  Sll(ctx, 4, bus(State_32_32.DEN), den32, bus(&State_32_32.SHIFT)); // den<<=shift

  // 48-bit widening SLL on numerator by shift
  Emit(ctx, bus(State_32_32.NUM), num32, 4);
  Emit(ctx, bus_(State_32_32.NUM, 4), bus(ZERO), 2);
  Sll(ctx, 6, bus(State_32_32.NUM), bus(State_32_32.NUM), bus(&State_32_32.SHIFT));

  // qhat0 = divwhu(num2,num1,den1,&rhat0);
  // c1 = qhat0 * den0
  // c2 = rhat0 * b + num0
  // idx = 0u16, 1u16, 2u16
  // q0 = subhhuh(qhat0, idx)
  // idx = if c1 GT c2 { if c1 - c2 GT den32 then 2u16 else 1u16 } else 0u16

  Div_32_16(ctx,
            bus(&State_32_32.QHAT),
            bus(&State_32_32.RHAT),
            bus(&State_32_32.NUM[1]),
            bus(&State_32_32.DEN[1]));

  uint32_t *MULTIPLIER_PREP = MonomorphizeOn(0, arena_alloc(&ARENA, 4, 4));
  Emit(ctx, bus(MULTIPLIER_PREP), bus(&State_32_32.DEN[0]), 2);
  // should be 32x16->32 multiply, but i don't have one of those yet
  UMul(ctx, 4, bus(&State_32_32.C1), bus(&State_32_32.QHAT), bus(MULTIPLIER_PREP));

  Emit(ctx, bus(&State_32_32.C2), bus(&State_32_32.NUM[0]), 2);
  Emit(ctx, bus_(&State_32_32.C2, 2), bus(&State_32_32.RHAT), 2);
  cblk *_skip_or = Last(ctx);

  uint8_t *IDCS = MonomorphizeOn(0, arena_alloc(&ARENA, 8, 256));
  for (int i = 0; i < 8; i++)
    IDCS[i] = 0;
  IDCS[4 | 0] = 1;
  IDCS[4 | 1] = 2;

  // 2. Run OR
  // XXX: This runs AFTER (1.)
  busad $index_or = Label(ctx);
  cblk *_load_set = Emit(ctx, bus_null(), bus_(HYPER, HYPER_OR), 1);

  // 1. Run idx := if c1>c2 then (if (c1-c2)>den32 then 2 else 1) else 0
  _skip_or->next_cb = Label(ctx);
  Geu(ctx,
      4,
      bus_rel_fld($index_or, +0, FLD_SRC, 1),
      bus(&State_32_32.C2),
      bus(&State_32_32.C1),
      ICF_INV | ICF_X4);
  Sub(ctx, 4, bus(&State_32_32.CDIFF), bus(&State_32_32.C1), bus(&State_32_32.C2));
  Geu(ctx,
      4,
      bus_rel_fld($index_or, +0, FLD_SRC, 2),
      bus(State_32_32.DEN),
      bus(&State_32_32.CDIFF),
      ICF_INV);
  Branch(ctx, $index_or);

  // 3. Run q0 := qhat - idx
  uint16_t *SUBTRAHEND = MonomorphizeOn(0, arena_alloc(&ARENA, 2, 4));
  *SUBTRAHEND = 0;
  busad $index_load = Label(ctx);
  _load_set->next_cb = $index_load;
  _load_set->dst_addr = bus_rel_fld($index_load, +0, FLD_SRC, 0);
  Emit(ctx, bus(SUBTRAHEND), bus(IDCS), 1);
  Sub(ctx, 2, quot16, bus(&State_32_32.QHAT), bus(SUBTRAHEND));

  // REM = NUM - q0_16 * DEN
  // rem32 = REM >> shift
  // note, however, that this is equivalent to
  // rem32 = num32 - q0_16 * den32

  // Again: really need a 32x16->32 multiplier
  Emit(ctx, bus(MULTIPLIER_PREP), quot16, 2);
  UMul(ctx, 4, bus(&State_32_32.PROD), bus(MULTIPLIER_PREP), den32);
  Sub(ctx, 4, rem32, num32, bus(&State_32_32.PROD));
}

static volatile uint8_t WHICH_0000 = 128;
static volatile uint8_t WHICH_000x = 129;
static volatile uint8_t WHICH_00xx = 130;
static volatile uint8_t WHICH_xxxx = 131;

static volatile uint8_t DIV_WHICH = 0;

struct branches
{
  volatile alignas(256) uint32_t IBR[4];
  volatile alignas(256) uint32_t SBR[4];
};

struct UDiv32_Exits
{
  cblk *w0u;
  cblk *wbu;
  cblk *whu;
  cblk *wwu;
};

INITIALIZER(div_equzh)
{
  for (int i = 0; i < (1 << 16); i++)
    HYPER[i << 8 | HYPER_EQUZH] = ((i & 0x00ff) ? +8 : +0) + ((i & 0xff00) ? +4 : +0);
}

// struct UDiv32Exits
void
UDiv32(emit_ctx *ctx, busad quot32, busad rem32, busad num32, busad den32)
{
  struct branches *branches = arena_alloc(&ARENA, sizeof *branches, 1 << 8);
  __builtin_memset(branches, 0, sizeof *branches);

  busad $0 = Label(ctx);
  Emit(ctx, bus_rel_fld($0, +1, FLD_SRC, 1), bus_add(den32, 2), 2);
  Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 0), bus_(HYPER, HYPER_EQUZH), 1);
  Emit(ctx, bus_rel_fld($0, +3, FLD_NXT, 0), bus(branches->IBR), 4);
  Emit(ctx, bus(ZERO), bus(ZERO), 4);

  branches->IBR[0] = Label(ctx)._0; // den=[0,0,x,x]

  cblk *_end_whu_branch, *_end_wbu_branch, *_end_w0u_branch;
  {
    busad $1 = Label(ctx);
    Emit(ctx, bus_rel_fld($1, +1, FLD_SRC, 1), bus_add(den32, 0), 2);
    Emit(ctx, bus_rel_fld($1, +2, FLD_SRC, 0), bus_(HYPER, HYPER_EQUZH), 1);
    Emit(ctx, bus_rel_fld($1, +3, FLD_NXT, 0), bus(branches->SBR), 4);
    Emit(ctx, bus(ZERO), bus(ZERO), 4);

    branches->SBR[0] = Label(ctx)._0; // den=[0,0,0,0]
    Emit(ctx, bus(&DIV_WHICH), bus(&WHICH_0000), 1);
    static volatile uint32_t K_NEG1 = 0xffffffff;
    Emit(ctx, quot32, bus(&K_NEG1), 4);
    Emit(ctx, rem32, num32, 4);
    _end_w0u_branch = Last(ctx);

    branches->SBR[2] = Label(ctx)._0; // den=[0,0,0,den0]
    Emit(ctx, bus(&DIV_WHICH), bus(&WHICH_000x), 1);
    Div_32_8(ctx, quot32, rem32 /*as 8*/, num32, den32 /*as 8*/);
    Emit(ctx, bus_add(rem32, 1), bus(ZERO), 3);
    _end_wbu_branch = Last(ctx);

    branches->SBR[1] = branches->SBR[3] = Label(ctx)._0; // den=[0,0,den1,?den0]
    Emit(ctx, bus(&DIV_WHICH), bus(&WHICH_00xx), 1);
    Div_32_16(ctx, quot32 /*as 24*/, rem32 /*as 16*/, num32, den32 /*as 16*/);
    Emit(ctx, bus_add(rem32, 2), bus(ZERO), 2);
    Emit(ctx, bus_add(quot32, 3), bus(ZERO), 1);
    _end_whu_branch = Last(ctx);
  }

  branches->IBR[1] = branches->IBR[2] = branches->IBR[3] =
    Label(ctx)._0; // (den>>16) != 0 -> divwwu
  Emit(ctx, bus(&DIV_WHICH), bus(&WHICH_xxxx), 1);
  Div_32_32(ctx, quot32 /*as 16*/, rem32, num32, den32);
  Emit(ctx, bus_add(quot32, 2), bus(ZERO), 2);

  _end_w0u_branch->next_cb = Label(ctx);
  _end_wbu_branch->next_cb = Label(ctx);
  _end_whu_branch->next_cb = Label(ctx);
  Emit(ctx, bus_null(), bus_null(), 0);

  // return (struct UDiv32Exits){
  //   .w0u = _end_w0u_branch,
  //   .wbu = _end_wbu_branch,
  //   .whu = _end_whu_branch,
  //   .wwu = Last(ctx),
  // };
}

struct SDiv32_Tables
{
  // indirection tables
  volatile alignas(256) uint32_t A_ABS[2];
  volatile alignas(256) uint32_t B_ABS[2];
  volatile alignas(1 << 16) uint32_t QUOT_SIGN_TABLE[1 << 16];
  volatile alignas(256) uint32_t QUOT_ZERO_TABLE[2];
  volatile alignas(256) uint32_t REM_SIGN_TABLE[2];
};

INITIALIZER(sdiv_equzb4)
{
  for (int i = 0; i < 256; i++)
    SUPER[i << 8 | SUPER_SGNB4] = (i & 0x80) ? 4 : 0;
}

void
SDiv32(emit_ctx *ctx, busad quot, busad rem, busad a32, busad b32)
{
  struct SDiv32_Tables *tables = MonomorphizeOn(0, arena_alloc(&ARENA, sizeof *tables, 1 << 16));
  memset(tables, 0, sizeof *tables);

  volatile uint32_t *X_A = MonomorphizeOn(0, arena_alloc(&ARENA, sizeof *X_A, 4));
  volatile uint32_t *X_B = MonomorphizeOn(0, arena_alloc(&ARENA, sizeof *X_B, 4));
  volatile uint32_t *B_Z = MonomorphizeOn(0, arena_alloc(&ARENA, sizeof *B_Z, 4));
  volatile uint32_t *A_U = MonomorphizeOn(0, arena_alloc(&ARENA, sizeof *A_U, 4));
  volatile uint32_t *B_U = MonomorphizeOn(0, arena_alloc(&ARENA, sizeof *B_U, 4));
  *X_A = *X_B = *B_Z = 0;

  busad $0 = Label(ctx);
  Emit(ctx, bus_rel_fld($0, +1, FLD_SRC, 1), bus_add(a32, 3), 1);
  Emit(ctx, bus(&X_A), bus_(SUPER, SUPER_SGNB4), 1);
  Emit(ctx, bus_rel_fld($0, +3, FLD_SRC, 1), bus_add(b32, 3), 1);
  Emit(ctx, bus(&X_B), bus_(SUPER, SUPER_SGNB4), 1);
  Equ(ctx, 4, bus(&B_Z), b32, bus(ZERO), ICF_X4);

  busad $load_a_abs = Label(ctx);
  Emit(ctx, bus_rel_fld($load_a_abs, +1, FLD_SRC, 0), bus(&X_A), 1);
  Emit(ctx, bus_rel_fld($load_a_abs, +2, FLD_NXT, 0), bus(tables->A_ABS), 4);
  cblk *_xa_to_sign_table = Here(ctx);
  Emit(ctx, bus(ZERO), bus(&X_A), 1);

  tables->A_ABS[1] = Label(ctx)._0;
  Neg(ctx, 4, bus(&A_U), a32);
  Branch(ctx, bus_rel_cb(Label(ctx), +1));
  tables->A_ABS[0] = Label(ctx)._0;
  Emit(ctx, bus(&A_U), a32, 4);

  busad $load_b_abs = Label(ctx);
  Emit(ctx, bus_rel_fld($load_b_abs, +1, FLD_SRC, 0), bus(&X_B), 1);
  Emit(ctx, bus_rel_fld($load_b_abs, +2, FLD_NXT, 0), bus(tables->B_ABS), 4);
  cblk *_xb_to_sign_table = Here(ctx);
  Emit(ctx, bus(ZERO), bus(&X_B), 1);

  tables->B_ABS[1] = Label(ctx)._0;
  Neg(ctx, 4, bus(&B_U), b32);
  Branch(ctx, bus_rel_cb(Label(ctx), +1));
  tables->B_ABS[0] = Label(ctx)._0;
  Emit(ctx, bus(&B_U), b32, 4);

  cblk *_xa_to_rem_sign_table = Here(ctx);
  Emit(ctx, bus(ZERO), bus(&X_A), 1);
  cblk *_bz_to_qzero_table = Here(ctx);
  Emit(ctx, bus(ZERO), bus(&B_Z), 1);
  UDiv32(ctx, quot, rem, bus(&A_U), bus(&B_U));

  busad $qzero_dispatch = Label(ctx);
  // udiv_exits.w0u->next_cb = udiv_exits.wbu->next_cb = udiv_exits.whu->next_cb =
  //   udiv_exits.wwu->next_cb = $qzero_dispatch;
  // udivmod_exits_set_next(udiv_exits, $qzero_dispatch);
  _bz_to_qzero_table->dst_addr = bus_rel_fld($qzero_dispatch, +0, FLD_SRC, 0);
  Emit(ctx, bus_rel_fld($qzero_dispatch, +1, FLD_NXT, 0), bus(tables->QUOT_ZERO_TABLE), 4);
  Emit(ctx, bus(ZERO), bus(ZERO), 4);

  busad $quot_dispatch = Label(ctx);
  tables->QUOT_ZERO_TABLE[0] = $quot_dispatch._0;
  _xa_to_sign_table->dst_addr = bus_rel_fld($quot_dispatch, +0, FLD_SRC, 0);
  _xb_to_sign_table->dst_addr = bus_rel_fld($quot_dispatch, +0, FLD_SRC, 1);
  Emit(ctx, bus_rel_fld($quot_dispatch, +1, FLD_NXT, 0), bus(tables->QUOT_SIGN_TABLE), 4);
  Emit(ctx, bus(ZERO), bus(ZERO), 4);

  busad $qneg = Label(ctx);
  tables->QUOT_SIGN_TABLE[0x100] = tables->QUOT_SIGN_TABLE[0x001] = $qneg._0;
  Neg(ctx, 4, quot, quot);

  busad $rem_dispatch = Label(ctx);
  tables->QUOT_SIGN_TABLE[0x000] = tables->QUOT_SIGN_TABLE[0x101] = $rem_dispatch._0;
  tables->QUOT_ZERO_TABLE[1] = $rem_dispatch._0;
  _xa_to_rem_sign_table->dst_addr = bus_rel_fld($rem_dispatch, +0, FLD_SRC, 0);
  Emit(ctx, bus_rel_fld($rem_dispatch, +1, FLD_NXT, 0), bus(tables->REM_SIGN_TABLE), 4);
  Emit(ctx, bus(ZERO), bus(ZERO), 4);

  busad $rneg = Label(ctx);
  tables->REM_SIGN_TABLE[1] = $rneg._0;
  Neg(ctx, 4, rem, rem);

  tables->REM_SIGN_TABLE[0] = Label(ctx)._0;
  Emit(ctx, bus(ZERO), bus(ZERO), 4);
}
