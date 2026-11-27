#include <bcm2835/dma/arith.h>




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

  if(width > 1){
    EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), lhs, width, 1, 31, 0);
    EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), rhs, width, 1, 31, 0);
  } else {
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), lhs, 1);
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), rhs, 1);
  }

  for (i = 0; i < width; i++)
    Emit(ctx, bus_add(ret, i), bus_(HYPER, HYPER_AND), 1);
}




/* -------------------------------------------------------------------------------------------------
   BIT CLEAR */




INITIALIZER(bic)
{
  for(uint16_t i = 0;i < 256;i++)
    for(uint16_t j = 0;j < 256;j++)
      HYPER[i << 16 | j << 8 | HYPER_BIC] = i & (~j);
}

void
Bic(emit_ctx *ctx, size_t width, busad ret, busad lhs, busad rhs)
{
  size_t i;
  busad $0 = Label(ctx);

  if(width > 1){
    EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), lhs, width, 1, 31, 0);
    EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), rhs, width, 1, 31, 0);
  } else {
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), lhs, 1);
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), rhs, 1);
  }

  for(i = 0;i < width;i++)
    Emit(ctx, bus_add(ret, i), bus_(HYPER, HYPER_BIC), 1);
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

  if(width > 1){
    EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), lhs, width, 1, 31, 0);
    EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), rhs, width, 1, 31, 0);
  } else {
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), lhs, 1);
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), rhs, 1);
  }

  for (i = 0; i < width; i++)
    Emit(ctx, bus_add(ret, i), bus_(HYPER, HYPER_OR), 1);
}
