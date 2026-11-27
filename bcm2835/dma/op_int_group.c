#include <bcm2835/dma/private.h>
#include <bcm2835/dma/emit.h>
#include <bcm2835/dma/arith.h>

#include <generic/math.h>




/* -------------------------------------------------------------------------------------------------
   INCREMENT */




INITIALIZER(inc_1)
{
  uint16_t i;

  for (i = 0; i < 256; i++) {
    SUPER[(i << 8 | SUPER_INC) + 0] = (i + 0) & 0xff;
    SUPER[(i << 8 | SUPER_INC) + 1] = (i + 0) >> 8 ? SUPER_INC + 2 : SUPER_INC;
    SUPER[(i << 8 | SUPER_INC) + 2] = (i + 1) & 0xff;
    SUPER[(i << 8 | SUPER_INC) + 3] = (i + 1) >> 8 ? SUPER_INC + 2 : SUPER_INC;
  }
}

void
Inc(emit_ctx *ctx, size_t width, busad ret, busad inp)
{
  size_t i;
  assert(width);

  busad $0 = Label(ctx);

  if (width > 1)
    EmitStrided(ctx, bus_rel_fld($0, +1, FLD_SRC, 1), inp, width, 1, 63, 0);
  else
    Emit(ctx, bus_rel_fld($0, +1, FLD_SRC, 1), inp, 1);

  for (i = 0; i < width; i++) {
    busad $_1 = Label(ctx);
    Emit(ctx, bus_add(ret, i), bus_(SUPER, SUPER_INC + 2), (i < width - 1) ? 2 : 1);
    if (i < width - 1)
      // Emit(ctx, bus_rel_fld($_1, +2, FLD_SRC, +0), bus_(SUPER, SUPER_INC + 1), 1);
      Emit(ctx, bus_rel_fld($_1, +2, FLD_SRC, +0), bus_add(ret, i + 1), 1);
  }
}




INITIALIZER(inc_4)
{
  uint16_t i;

  for (i = 0; i < 256; i++) {
    SUPER[(i << 8 | SUPER_INC4) + 0] = (i + 0) & 0xff;
    SUPER[(i << 8 | SUPER_INC4) + 1] = (i + 0) >> 8 ? SUPER_INC4 + 2 : SUPER_INC4;
    SUPER[(i << 8 | SUPER_INC4) + 2] = (i + 4) & 0xff;
    SUPER[(i << 8 | SUPER_INC4) + 3] = (i + 4) >> 8 ? SUPER_INC4 + 2 : SUPER_INC4;
  }
}

void
Inc4(emit_ctx *ctx, size_t width, busad ret, busad inp)
{
  size_t i;
  assert(width);

  busad $0 = Label(ctx);

  if (width > 1)
    EmitStrided(ctx, bus_rel_fld($0, +1, FLD_SRC, 1), inp, width, 1, 63, 0);
  else
    Emit(ctx, bus_rel_fld($0, +1, FLD_SRC, 1), inp, 1);

  for (i = 0; i < width; i++) {
    busad $_1 = Label(ctx);
    Emit(ctx, bus_add(ret, i), bus_(SUPER, SUPER_INC4 + 2), (i < width - 1) ? 2 : 1);
    if (i < width - 1)
      // Emit(ctx, bus_rel_fld($_1, +2, FLD_SRC, +0), bus_(SUPER, SUPER_INC4 + 1), 1);
      Emit(ctx, bus_rel_fld($_1, +2, FLD_SRC, +0), bus_add(ret, i + 1), 1);
  }
}




/* -------------------------------------------------------------------------------------------------
   ADDITION */




INITIALIZER(add)
{
  for (uint16_t i = 0; i < 256; i++)
    for (uint16_t j = 0; j < 256; j++)
      for (uint16_t l = 0; l < 2; l++) {
        uint16_t r = i + j + l;
        HYPER[(i << 16 | j << 8 | HYPER_ADD) + 2 * l + 0] = r & 255;
        HYPER[(i << 16 | j << 8 | HYPER_ADD) + 2 * l + 1] = (r >> 8) ? HYPER_ADD + 2 : HYPER_ADD;
      }
}

void
Add(emit_ctx *ctx, size_t width, busad ret, busad lhs, busad rhs)
{
  size_t i;
  assert(width);

  busad $0 = Label(ctx);

  if (width > 1) {
    EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), rhs, width, 1, 63, 0);
    EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), lhs, width, 1, 63, 0);
  } else {
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), rhs, 1);
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), lhs, 1);
  }

  Emit(ctx, bus_add(ret, 0), bus_(HYPER, HYPER_ADD), 2);

  for (i = 1; i < width; i++) {
    busad $_1 = Label(ctx);
    Emit(ctx, bus_rel_fld($_1, +1, FLD_SRC, 0), bus_add(ret, i), 1);
    Emit(ctx, bus_add(ret, i), bus(HYPER), (i < width - 1) ? 2 : 1);
  }
}




/* -------------------------------------------------------------------------------------------------
   SUBTRACTION */




INITIALIZER(sub)
{
  for (uint16_t i = 0; i < 256; i++)
    for (uint16_t j = 0; j < 256; j++)
      for (uint16_t l = 0; l < 2; l++) {
        uint16_t r = i - j - l;
        HYPER[(i << 16 | j << 8 | HYPER_SUB) + 2 * l + 0] = r & 255;
        HYPER[(i << 16 | j << 8 | HYPER_SUB) + 2 * l + 1] = (r >> 8) ? HYPER_SUB + 2 : HYPER_SUB;
      }
}

void
Sub(emit_ctx *ctx, size_t width, busad ret, busad lhs, busad rhs)
{
  size_t i;
  assert(width);

  busad $0 = Label(ctx);

  if (width > 1) {
    EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), rhs, width, 1, 63, 0);
    EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), lhs, width, 1, 63, 0);
  } else {
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), rhs, 1);
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), lhs, 1);
  }

  Emit(ctx, bus_add(ret, 0), bus_(HYPER, HYPER_SUB), 2);

  for (i = 1; i < width; i++) {
    busad $_1 = Label(ctx);
    Emit(ctx, bus_rel_fld($_1, +1, FLD_SRC, 0), bus_add(ret, i), 1);
    Emit(ctx, bus_add(ret, i), bus(HYPER), (i < width - 1) ? 2 : 1);
  }
}




/* -------------------------------------------------------------------------------------------------
   NEGATION */




void
Neg(emit_ctx *ctx, size_t width, busad ret, busad inp)
{
  volatile uint8_t *tmp;

  // TODO: This can be optimized by fusing the Not and Inc operations.

  assert(width < 256);

  tmp = MonomorphizeOn(width, ({ arena_alloc(&ARENA, width, min(min(width, 4), 16)); }));

  Not(ctx, width, bus(tmp), inp);
  Inc(ctx, width, ret, bus(tmp));
}




// /*
// -------------------------------------------------------------------------------------------------
//    MULTIPLICATION */




// void
// UMulQWW(emit_ctx *ctx, busad ret64, busad lhs32, busad rhs32)
// {

//   const busad a0 = bus_add(lhs32, 0), a1 = bus_add(lhs32, 1), a2 = bus_add(lhs32, 2),
//               a3 = bus_add(lhs32, 3);

//   const busad b0 = bus_add(rhs32, 0), b1 = bus_add(rhs32, 1), b2 = bus_add(rhs32, 2),
//               b3 = bus_add(rhs32, 3);

//   const busad r0 = bus_add(ret64, 0), r1 = bus_add(ret64, 1), r2 = bus_add(ret64, 2),
//               r3 = bus_add(ret64, 3), r4 = bus_add(ret64, 4), r5 = bus_add(ret64, 5),
//               r6 = bus_add(ret64, 6);

//   //
//   -----------------------------------------------------------------------------------------------
//   // CONVENTION: hyper offset #2 is used for multiplication scatter, offset #1 is used for
//   addition
//   //             scatter
//   //
//   -----------------------------------------------------------------------------------------------

//   enum
//   {
//     SCA_MULTIPLY = +2,
//     SCA_ADD = +1,
//   };

//   // a0b0

//   {
//     busad $0 = Label(ctx);
//     Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), a0, 1);
//     Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), b0, 1);
//     Emit(ctx, r0, bus_(HYPER, HYPER_MUL), 2); // a0b0
//   }

//   // V + a0b1 + a1b0

//   {
//     cblk *_1_0, *_1_1;

//     {
//       busad $1 = Label(ctx);
//       EmitStrided(ctx, bus_rel_fld($1, +2, FLD_SRC, 1), a0, 2, 1, +31, +0);
//       EmitStrided(ctx, bus_rel_fld($1, +2, FLD_SRC, 2), b1, 2, 1, +31, -2);
//       _1_0 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a0b1
//       _1_1 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a1b0
//     }

//     // ( V.1 +.2 a0b1.2 ).3 +.3 a1b0.2

//     static volatile uint32_t o_1;
//     // Probe("o_1", &o_1, 4);

//     {
//       busad $1_0 = Label(ctx);                                        // V.1 +.2 a0b1.2 ->.3 O1.3
//       Emit(ctx, bus_rel_fld($1_0, +1, FLD_SRC, 1), r1, 1);            // load V_0
//       _1_0->dst_addr = bus_rel_fld($1_0, +1, FLD_SRC, +SCA_MULTIPLY); // Add.2
//       Emit(ctx, bus(&o_1), bus_(HYPER, HYPER_ADD), 2);                // V_0 + a0b1_0 -> O1_0.2
//       Emit(ctx, bus_rel_fld($1_0, +3, FLD_SRC, 0), bus_(&o_1, 1), 1); // O1_1
//       Emit(ctx, bus_(&o_1, 1), bus_(HYPER, HYPER_ADD), 2);            // R_2 + a0b1_1 -> O1_1.2
//     }

//     {
//       busad $1_1 = Label(ctx); // O1.3 +.3 a1b0.2 -> R_1.3
//       EmitStrided(ctx, bus_rel_fld($1_1, +1, FLD_SRC, 1), bus(&o_1), 2, 1, +63, +0);
//       _1_1->dst_addr = bus_rel_fld($1_1, +1, FLD_SRC, +SCA_MULTIPLY); // Add.3
//       Emit(ctx, bus_add(r1, 0), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($1_1, +3, FLD_SRC, 0), bus_add(r1, 1), 1);
//       Emit(ctx, bus_add(r1, 1), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($1_1, +5, FLD_SRC, 0), bus_add(r1, 2), 1);
//       Emit(ctx, bus_add(r1, 2), bus_(HYPER, HYPER_ADD), 1);
//     }
//   }

//   // V + a0b2 + a1b1 + a2b0

//   {
//     cblk *_2_0, *_2_1, *_2_2;
//     {
//       busad $2 = Label(ctx);
//       EmitStrided(ctx, bus_rel_fld($2, +2, FLD_SRC, 1), a0, 3, 1, +31, +0);
//       EmitStrided(ctx, bus_rel_fld($2, +2, FLD_SRC, 2), b2, 3, 1, +31, -2);
//       _2_0 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a0b2
//       _2_1 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a1b1
//       _2_2 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a2b0
//     }

//     static volatile uint32_t o_2a, o_2b;
//     // Probe("o_2a", &o_2a, 4);
//     // Probe("o_2b", &o_2b, 4);

//     // ((V.2 +.2 a0b2.2).3 +.3 a1b1.2) +.3 a2b0.2

//     {
//       busad $2_0 = Label(ctx); // R_2.2 +.2 a0b2.2 ->.3 O2A.3
//       EmitStrided(ctx, bus_rel_fld($2_0, +1, FLD_SRC, +SCA_ADD), r2, 2, 1, +63, +0); // load
//       R_2.2 _2_0->dst_addr = bus_rel_fld($2_0, +1, FLD_SRC, +SCA_MULTIPLY);                //
//       Add.3 Emit(ctx, bus_(&o_2a, 0), bus_(HYPER, HYPER_ADD), 2); Emit(ctx, bus_rel_fld($2_0, +3,
//       FLD_SRC, 0), bus_(&o_2a, 1), 1); Emit(ctx, bus_(&o_2a, 1), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($2_0, +5, FLD_SRC, 0), bus_(&o_2a, 2), 1);
//       Emit(ctx, bus_(&o_2a, 2), bus_(HYPER, HYPER_ADD), 1);
//     }

//     {
//       busad $2_1 = Label(ctx); // O2A.3 +.3 a1b1.3 ->.3 O2B.3
//       EmitStrided(ctx, bus_rel_fld($2_1, +1, FLD_SRC, +SCA_ADD), bus(&o_2a), 3, 1, +63, +0);
//       _2_1->dst_addr = bus_rel_fld($2_1, +1, FLD_SRC, +SCA_MULTIPLY);
//       Emit(ctx, bus_(&o_2b, 0), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($2_1, +3, FLD_SRC, 0), bus_(&o_2b, 1), 1);
//       Emit(ctx, bus_(&o_2b, 1), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($2_1, +5, FLD_SRC, 0), bus_(&o_2b, 2), 1);
//       Emit(ctx, bus_(&o_2b, 2), bus_(HYPER, HYPER_ADD), 1);
//     }

//     {
//       busad $2_2 = Label(ctx); // O2B.3 +.3 a2b0.2 ->.3 R_2.3
//       EmitStrided(ctx, bus_rel_fld($2_2, +1, FLD_SRC, +SCA_ADD), bus(&o_2b), 3, 1, +63, +0);
//       _2_2->dst_addr = bus_rel_fld($2_2, +1, FLD_SRC, +SCA_MULTIPLY);
//       Emit(ctx, bus_add(r2, 0), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($2_2, +3, FLD_SRC, 0), bus_add(r2, 1), 1);
//       Emit(ctx, bus_add(r2, 1), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($2_2, +5, FLD_SRC, 0), bus_add(r2, 2), 1);
//       Emit(ctx, bus_add(r2, 2), bus_(HYPER, HYPER_ADD), 1);
//     }
//   }

//   // V + a0b3 + a1b2 + a2b1 + a3b0

//   {
//     cblk *_3_0, *_3_1, *_3_2, *_3_3;
//     {
//       busad $3 = Label(ctx);
//       EmitStrided(ctx, bus_rel_fld($3, +2, FLD_SRC, 1), a0, 4, 1, +31, +0);
//       EmitStrided(ctx, bus_rel_fld($3, +2, FLD_SRC, 2), b3, 4, 1, +31, -2);
//       _3_0 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a0b3
//       _3_1 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a1b2
//       _3_2 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a2b1
//       _3_3 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a3b0
//     }

//     static volatile uint32_t o_3a, o_3b, o_3c;
//     // Probe("o_3a", &o_3a, 4);
//     // Probe("o_3b", &o_3b, 4);
//     // Probe("o_3c", &o_3c, 4);

//     // (V.2 +.2 a0b3.2).3
//     //   +.3 a1b2.2
//     //   +.3 a2b1.2
//     //   +.3 a3b0.2

//     {
//       busad $3_0 = Label(ctx); // V.2 +.2 a0b3.2 ->.3 O3A.3
//       EmitStrided(ctx, bus_rel_fld($3_0, +1, FLD_SRC, +SCA_ADD), r3, 2, 1, +63, +0);
//       _3_0->dst_addr = bus_rel_fld($3_0, +1, FLD_SRC, +SCA_MULTIPLY);
//       Emit(ctx, bus_(&o_3a, 0), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($3_0, +3, FLD_SRC, 0), bus_(&o_3a, 1), 1);
//       Emit(ctx, bus_(&o_3a, 1), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($3_0, +5, FLD_SRC, 0), bus_(&o_3a, 2), 1);
//       Emit(ctx, bus_(&o_3a, 2), bus_(HYPER, HYPER_ADD), 1);
//     }

//     {
//       busad $3_1 = Label(ctx); // O3A.3 +.3 a1b2.2 ->.3 O3B.3
//       EmitStrided(ctx, bus_rel_fld($3_1, +1, FLD_SRC, +SCA_ADD), bus(&o_3a), 3, 1, +63, +0);
//       _3_1->dst_addr = bus_rel_fld($3_1, +1, FLD_SRC, +SCA_MULTIPLY);
//       Emit(ctx, bus_(&o_3b, 0), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($3_1, +3, FLD_SRC, 0), bus_(&o_3b, 1), 1);
//       Emit(ctx, bus_(&o_3b, 1), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($3_1, +5, FLD_SRC, 0), bus_(&o_3b, 2), 1);
//       Emit(ctx, bus_(&o_3b, 2), bus_(HYPER, HYPER_ADD), 1);
//     }

//     {
//       busad $3_2 = Label(ctx); // O3B.3 +.3 a2b1.2 ->.3 O3C.3
//       EmitStrided(ctx, bus_rel_fld($3_2, +1, FLD_SRC, +SCA_ADD), bus(&o_3b), 3, 1, +63, +0);
//       _3_2->dst_addr = bus_rel_fld($3_2, +1, FLD_SRC, +SCA_MULTIPLY);
//       Emit(ctx, bus_(&o_3c, 0), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($3_2, +3, FLD_SRC, 0), bus_(&o_3c, 1), 1);
//       Emit(ctx, bus_(&o_3c, 1), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($3_2, +5, FLD_SRC, 0), bus_(&o_3c, 2), 1);
//       Emit(ctx, bus_(&o_3c, 2), bus_(HYPER, HYPER_ADD), 1);
//     }

//     {
//       busad $3_3 = Label(ctx); // O3C.3 +.3 a3b0.2 ->.3 R3.3
//       EmitStrided(ctx, bus_rel_fld($3_3, +1, FLD_SRC, +SCA_ADD), bus(&o_3c), 3, 1, +63, +0);
//       _3_3->dst_addr = bus_rel_fld($3_3, +1, FLD_SRC, +SCA_MULTIPLY);
//       Emit(ctx, bus_add(r3, 0), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($3_3, +3, FLD_SRC, 0), bus_add(r3, 1), 1);
//       Emit(ctx, bus_add(r3, 1), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($3_3, +5, FLD_SRC, 0), bus_add(r3, 2), 1);
//       Emit(ctx, bus_add(r3, 2), bus_(HYPER, HYPER_ADD), 1);
//     }
//   }

//   // V + a1b3 + a2b2 + a3b1

//   {
//     cblk *_4_0, *_4_1, *_4_2;
//     {
//       busad $4 = Label(ctx);
//       EmitStrided(ctx, bus_rel_fld($4, +2, FLD_SRC, 1), a1, 3, 1, +31, +0);
//       EmitStrided(ctx, bus_rel_fld($4, +2, FLD_SRC, 2), b3, 3, 1, +31, -2);
//       _4_0 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a1b3
//       _4_1 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a2b2
//       _4_2 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a3b1
//     }

//     static volatile uint32_t o_4a, o_4b;
//     // Probe("o_4a", &o_4a, 4);
//     // Probe("o_4b", &o_4b, 4);

//     // (V.2 +.2 a1b3.2).3
//     //   +.3 a2b2.2
//     //   +.3 a3b1.2

//     {
//       busad $4_0 = Label(ctx);
//       EmitStrided(ctx, bus_rel_fld($4_0, +1, FLD_SRC, +SCA_ADD), r4, 2, 1, +63, +0);
//       _4_0->dst_addr = bus_rel_fld($4_0, +1, FLD_SRC, +SCA_MULTIPLY);
//       Emit(ctx, bus_(&o_4a, 0), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($4_0, +3, FLD_SRC, 0), bus_(&o_4a, 1), 1);
//       Emit(ctx, bus_(&o_4a, 1), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($4_0, +5, FLD_SRC, 0), bus_(&o_4a, 2), 1);
//       Emit(ctx, bus_(&o_4a, 2), bus_(HYPER, HYPER_ADD), 1);
//     }

//     {
//       busad $4_1 = Label(ctx);
//       EmitStrided(ctx, bus_rel_fld($4_1, +1, FLD_SRC, +SCA_ADD), bus(&o_4a), 3, 1, +63, +0);
//       _4_1->dst_addr = bus_rel_fld($4_1, +1, FLD_SRC, +SCA_MULTIPLY);
//       Emit(ctx, bus_(&o_4b, 0), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($4_1, +3, FLD_SRC, 0), bus_(&o_4b, 1), 1);
//       Emit(ctx, bus_(&o_4b, 1), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($4_1, +5, FLD_SRC, 0), bus_(&o_4b, 2), 1);
//       Emit(ctx, bus_(&o_4b, 2), bus_(HYPER, HYPER_ADD), 1);
//     }

//     {
//       busad $4_2 = Label(ctx);
//       EmitStrided(ctx, bus_rel_fld($4_2, +1, FLD_SRC, +SCA_ADD), bus(&o_4b), 3, 1, +63, +0);
//       _4_2->dst_addr = bus_rel_fld($4_2, +1, FLD_SRC, +SCA_MULTIPLY);
//       Emit(ctx, bus_add(r4, 0), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($4_2, +3, FLD_SRC, 0), bus_add(r4, 1), 1);
//       Emit(ctx, bus_add(r4, 1), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($4_2, +5, FLD_SRC, 0), bus_add(r4, 2), 1);
//       Emit(ctx, bus_add(r4, 2), bus_(HYPER, HYPER_ADD), 1);
//     }
//   }

//   // V + a2b3 + a3b2

//   {
//     cblk *_5_0, *_5_1;
//     {
//       busad $5 = Label(ctx);
//       EmitStrided(ctx, bus_rel_fld($5, +2, FLD_SRC, 1), a2, 2, 1, +31, +0);
//       EmitStrided(ctx, bus_rel_fld($5, +2, FLD_SRC, 2), b3, 2, 1, +31, -2);
//       _5_0 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a2b3
//       _5_1 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a3b2
//     }

//     static volatile uint32_t o_5a;
//     // Probe("o_5a", &o_5a, 4);

//     // (V.2 +.2 a2b3.2).3
//     //  +.3 a3b2.2
//     //  ->.3 R5.3

//     {
//       busad $5_0 = Label(ctx);
//       EmitStrided(ctx, bus_rel_fld($5_0, +1, FLD_SRC, +SCA_ADD), r5, 2, 1, +63, +0);
//       _5_0->dst_addr = bus_rel_fld($5_0, +1, FLD_SRC, +SCA_MULTIPLY);
//       Emit(ctx, bus_(&o_5a, 0), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($5_0, +3, FLD_SRC, 0), bus_(&o_5a, 1), 1);
//       Emit(ctx, bus_(&o_5a, 1), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($5_0, +5, FLD_SRC, 0), bus_(&o_5a, 2), 1);
//       Emit(ctx, bus_(&o_5a, 2), bus_(HYPER, HYPER_ADD), 1);
//     }

//     {
//       busad $5_1 = Label(ctx);
//       EmitStrided(ctx, bus_rel_fld($5_1, +1, FLD_SRC, +SCA_ADD), bus(&o_5a), 3, 1, +63, +0);
//       _5_1->dst_addr = bus_rel_fld($5_1, +1, FLD_SRC, +SCA_MULTIPLY);
//       Emit(ctx, bus_add(r5, 0), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($5_1, +3, FLD_SRC, 0), bus_add(r5, 1), 1);
//       Emit(ctx, bus_add(r5, 1), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($5_1, +5, FLD_SRC, 0), bus_add(r5, 2), 1);
//       Emit(ctx, bus_add(r5, 2), bus_(HYPER, HYPER_ADD), 1);
//     }
//   }

//   // V + a3b3

//   {
//     cblk *_6_0;
//     {
//       busad $6 = Label(ctx);
//       Emit(ctx, bus_rel_fld($6, +2, FLD_SRC, 1), a3, 1);
//       Emit(ctx, bus_rel_fld($6, +2, FLD_SRC, 2), b3, 1);
//       _6_0 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a3b3
//     }

//     // (V.2 +.2 a3b3.2).2 ->.2 R6.2

//     {
//       busad $6_0 = Label(ctx);
//       EmitStrided(ctx, bus_rel_fld($6_0, +1, FLD_SRC, +SCA_ADD), r6, 2, 1, +63, +0);
//       _6_0->dst_addr = bus_rel_fld($6_0, +1, FLD_SRC, +SCA_MULTIPLY);
//       Emit(ctx, bus_add(r6, 0), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($6_0, +3, FLD_SRC, 0), bus_add(r6, 1), 1);
//       Emit(ctx, bus_add(r6, 1), bus_(HYPER, HYPER_ADD), 1);
//     }
//   }
// }

// void
// UMulWWW(emit_ctx *ctx, busad ret32, busad lhs32, busad rhs32)
// {

//   const busad a0 = bus_add(lhs32, 0);

//   const busad b0 = bus_add(rhs32, 0), b1 = bus_add(rhs32, 1), b2 = bus_add(rhs32, 2),
//               b3 = bus_add(rhs32, 3);

//   const busad r0 = bus_add(ret32, 0), r1 = bus_add(ret32, 1), r2 = bus_add(ret32, 2),
//               r3 = bus_add(ret32, 3);

//   //
//   -----------------------------------------------------------------------------------------------
//   // CONVENTION: hyper offset #2 is used for multiplication scatter, offset #1 is used for
//   addition
//   //             scatter
//   //
//   -----------------------------------------------------------------------------------------------

//   // a0b0 -> R0.2
//   // V + a0b1 + a1b0 -> R1.3
//   // a0b2 + a1b1 + a2b0 -> R2.2
//   // a0b3 + a1b2 + a2b1 + a3b0 -> R3.1

//   enum
//   {
//     SCA_MULTIPLY = +2,
//     SCA_ADD = +1,
//   };

//   // a0b0 -> R0.2

//   {
//     busad $0 = Label(ctx);
//     Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), a0, 1);
//     Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), b0, 1);
//     Emit(ctx, r0, bus_(HYPER, HYPER_MUL), 2); // a0b0
//   }

//   // V + a0b1 + a1b0 -> R1.3

//   {
//     cblk *_1_0, *_1_1;

//     {
//       busad $1 = Label(ctx);
//       EmitStrided(ctx, bus_rel_fld($1, +2, FLD_SRC, 1), a0, 2, 1, +31, +0);
//       EmitStrided(ctx, bus_rel_fld($1, +2, FLD_SRC, 2), b1, 2, 1, +31, -2);
//       _1_0 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a0b1
//       _1_1 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a1b0
//     }

//     // ( V.1 +.2 a0b1.2 ).3 +.3 a1b0.2

//     static volatile alignas(64) uint32_t o_1;
//     // Probe("o_1", &o_1, 4);

//     {
//       busad $1_0 = Label(ctx);                                        // V.1 +.2 a0b1.2 ->.3 O1.3
//       Emit(ctx, bus_rel_fld($1_0, +1, FLD_SRC, 1), r1, 1);            // load V_0
//       _1_0->dst_addr = bus_rel_fld($1_0, +1, FLD_SRC, +SCA_MULTIPLY); // Add.2
//       Emit(ctx, bus(&o_1), bus_(HYPER, HYPER_ADD), 2);                // V_0 + a0b1_0 -> O1_0.2
//       Emit(ctx, bus_rel_fld($1_0, +3, FLD_SRC, 0), bus_(&o_1, 1), 1); // O1_1
//       Emit(ctx, bus_(&o_1, 1), bus_(HYPER, HYPER_ADD), 2);            // R_2 + a0b1_1 -> O1_1.2
//     }

//     {
//       busad $1_1 = Label(ctx); // O1.3 +.3 a1b0.2 -> R_1.3
//       EmitStrided(ctx, bus_rel_fld($1_1, +1, FLD_SRC, 1), bus(&o_1), 2, 1, +63, +0);
//       _1_1->dst_addr = bus_rel_fld($1_1, +1, FLD_SRC, +SCA_MULTIPLY); // Add.3
//       Emit(ctx, bus_add(r1, 0), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($1_1, +3, FLD_SRC, 0), bus_add(r1, 1), 1);
//       Emit(ctx, bus_add(r1, 1), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($1_1, +5, FLD_SRC, 0), bus_add(r1, 2), 1);
//       Emit(ctx, bus_add(r1, 2), bus_(HYPER, HYPER_ADD), 1);
//     }
//   }

//   // V + a0b2 + a1b1 + a2b0 -> R2.2

//   {
//     cblk *_2_0, *_2_1, *_2_2;
//     {
//       busad $2 = Label(ctx);
//       EmitStrided(ctx, bus_rel_fld($2, +2, FLD_SRC, 1), a0, 3, 1, +31, +0);
//       EmitStrided(ctx, bus_rel_fld($2, +2, FLD_SRC, 2), b2, 3, 1, +31, -2);
//       _2_0 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a0b2
//       _2_1 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a1b1
//       _2_2 = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0); // a2b0
//     }

//     static volatile alignas(64) uint32_t o_2a, o_2b;
//     // Probe("o_2a", &o_2a, 4);
//     // Probe("o_2b", &o_2b, 4);

//     // ((V.2 +.2 a0b2.2).3 +.3 a1b1.2) +.3 a2b0.2

//     {
//       busad $2_0 = Label(ctx);
//       EmitStrided(ctx, bus_rel_fld($2_0, +1, FLD_SRC, +SCA_ADD), r2, 2, 1, +63, +0);
//       _2_0->dst_addr = bus_rel_fld($2_0, +1, FLD_SRC, +SCA_MULTIPLY);
//       Emit(ctx, bus_(&o_2a, 0), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($2_0, +3, FLD_SRC, 0), bus_(&o_2a, 1), 1);
//       Emit(ctx, bus_(&o_2a, 1), bus_(HYPER, HYPER_ADD), 1);
//     }

//     {
//       busad $2_1 = Label(ctx);
//       EmitStrided(ctx, bus_rel_fld($2_1, +1, FLD_SRC, +SCA_ADD), bus(&o_2a), 2, 1, +63, +0);
//       _2_1->dst_addr = bus_rel_fld($2_1, +1, FLD_SRC, +SCA_MULTIPLY);
//       Emit(ctx, bus_(&o_2b, 0), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($2_1, +3, FLD_SRC, 0), bus_(&o_2b, 1), 1);
//       Emit(ctx, bus_(&o_2b, 1), bus_(HYPER, HYPER_ADD), 1);
//     }

//     {
//       busad $2_2 = Label(ctx);
//       EmitStrided(ctx, bus_rel_fld($2_2, +1, FLD_SRC, +SCA_ADD), bus(&o_2b), 2, 1, +63, +0);
//       _2_2->dst_addr = bus_rel_fld($2_2, +1, FLD_SRC, +SCA_MULTIPLY);
//       Emit(ctx, bus_add(r2, 0), bus_(HYPER, HYPER_ADD), 2);
//       Emit(ctx, bus_rel_fld($2_2, +3, FLD_SRC, 0), bus_add(r2, 1), 1);
//       Emit(ctx, bus_add(r2, 1), bus_(HYPER, HYPER_ADD), 1);
//     }
//   }

//   // V + a0b3 + a1b2 + a2b1 + a3b0 -> R3.1

//   {
//     cblk *_3_0, *_3_1, *_3_2, *_3_3;
//     {
//       busad $3 = Label(ctx);
//       EmitStrided(ctx, bus_rel_fld($3, +2, FLD_SRC, 1), a0, 4, 1, +31, +0);
//       EmitStrided(ctx, bus_rel_fld($3, +2, FLD_SRC, 2), b3, 4, 1, +31, -2);
//       _3_0 = Emit(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 1); // a0b3
//       _3_1 = Emit(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 1); // a1b2
//       _3_2 = Emit(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 1); // a2b1
//       _3_3 = Emit(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 1); // a3b0
//     }

//     busad $3_0 = Label(ctx);
//     Emit(ctx, bus_rel_fld($3_0, +1, FLD_SRC, +SCA_ADD), r3, 1);
//     _3_0->dst_addr = bus_rel_fld($3_0, +1, FLD_SRC, +SCA_MULTIPLY);
//     Emit(ctx, bus_rel_fld($3_0, +2, FLD_SRC, +SCA_ADD), bus_(HYPER, HYPER_ADD), 1);
//     _3_1->dst_addr = bus_rel_fld($3_0, +2, FLD_SRC, +SCA_MULTIPLY);
//     Emit(ctx, bus_rel_fld($3_0, +3, FLD_SRC, +SCA_ADD), bus_(HYPER, HYPER_ADD), 1);
//     _3_2->dst_addr = bus_rel_fld($3_0, +3, FLD_SRC, +SCA_MULTIPLY);
//     Emit(ctx, bus_rel_fld($3_0, +4, FLD_SRC, +SCA_ADD), bus_(HYPER, HYPER_ADD), 1);
//     _3_3->dst_addr = bus_rel_fld($3_0, +4, FLD_SRC, +SCA_MULTIPLY);
//     Emit(ctx, r3, bus_(HYPER, HYPER_ADD), 1);
//   }
// }
