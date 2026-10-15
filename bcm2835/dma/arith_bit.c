#include <bcm2835/dma/private.h>
#include <bcm2835/dma/emit.h>
#include <bcm2835/dma/arith.h>




/* -------------------------------------------------------------------------------------------------
   BITWISE NOT */




_TableSymbol volatile uint8_t alignas(1 << 8) NOT[1 << 8];

INITIALIZER(not)
{
  for (uint16_t i = 0; i < 256; i++)
    NOT[i] = ~i;
}

void
Not(emit_ctx *ctx, size_t width, busad ret, busad inp)
{
  size_t i;
  assert(width);
  assert(width < 16);

  // 565
  busad $0 = Label(ctx);
  EmitStrided(ctx, bus_rel_fld($0, +1, FLD_SRC, 0), inp, width, 1, 31, 0);
  for (i = 0; i < width; i++)
    Emit(ctx, bus_add(ret, i), bus(NOT), 1);

  // 708
  // for(i = 0;i < width;i++) {
  //   busad $0 = Label(ctx);
  //   Emit(ctx, bus_rel_fld($0,+1,FLD_SRC,0), bus_add(inp,i), 1);
  //   Emit(ctx, bus_add(ret, i), bus(NOT), 1);
  // }
}




/* -------------------------------------------------------------------------------------------------
   BITWISE AND */




INITIALIZER(and)
{
  for (uint16_t i = 0; i < 256; i++)
    for (uint16_t j = 0; j < 256; j++)
      HYPER[i << 16 | j << 8 | HYPER_AND] = i & j;
}

void
And(emit_ctx *ctx, size_t width, busad ret, busad lhs, busad rhs)
{
  size_t i;
  busad $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), lhs, width, 1, 31, 0);
  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), rhs, width, 1, 31, 0);

  for (i = 0; i < width; i++)
    Emit(ctx, bus_add(ret, i), bus_(HYPER, HYPER_AND), 1);
}




/* -------------------------------------------------------------------------------------------------
   BITWISE OR */




INITIALIZER(or)
{
  for (uint16_t i = 0; i < 256; i++)
    for (uint16_t j = 0; j < 256; j++)
      HYPER[i << 16 | j << 8 | HYPER_OR] = i | j;
}

void
Or(emit_ctx *ctx, size_t width, busad ret, busad lhs, busad rhs)
{
  size_t i;
  busad $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), lhs, width, 1, 31, 0);
  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), rhs, width, 1, 31, 0);

  for (i = 0; i < width; i++)
    Emit(ctx, bus_add(ret, i), bus_(HYPER, HYPER_OR), 1);
}




/* -------------------------------------------------------------------------------------------------
   BITWISE XOR */




INITIALIZER(xor)
{
  for (uint16_t i = 0; i < 256; i++)
    for (uint16_t j = 0; j < 256; j++)
      HYPER[i << 16 | j << 8 | HYPER_XOR] = i ^ j;
}

void
Xor(emit_ctx *ctx, size_t width, busad ret, busad lhs, busad rhs)
{
  size_t i;
  busad $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), lhs, width, 1, 31, 0);
  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), rhs, width, 1, 31, 0);

  for (i = 0; i < width; i++)
    Emit(ctx, bus_add(ret, i), bus_(HYPER, HYPER_XOR), 1);
}
