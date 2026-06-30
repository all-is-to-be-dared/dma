#include <stdint.h>
#include <string.h>

#include <bcm2835-dma/private.h>
#include <bcm2835-dma/emit.h>
#include <bcm2835-dma/arith.h>




[[gnu::used]] volatile uint8_t alignas(1 << 24) SUPER[1 << 24];

enum
{
  SUPER_ADD = 0x04, // 4
  SUPER_SUB = 0x08, // 4
  SUPER_AND = 0x0c, // 1
  SUPER_OR  = 0x0d, // 1
  SUPER_XOR = 0x0e, // 1

  SUPER_SLL = 0x14, // 8
};




/* -------------------------------------------------------------------------------------------------
   ADDITION */




INITIALIZER(init_add)
{
  for (uint16_t i = 0; i < 256; i++)
    for (uint16_t j = 0; j < 256; j++)
      for (uint16_t l = 0; l < 2; l++) {
        uint16_t r = i + j + l;
        SUPER[(i << 16 | j << 8 | SUPER_ADD) + 2 * l + 0] = r & 255;
        SUPER[(i << 16 | j << 8 | SUPER_ADD) + 2 * l + 1] = (r >> 8) ? SUPER_ADD + 2 : SUPER_ADD;
      }
}

void
Add(emit_ctx* ctx, size_t width, busad ret, busad lhs, busad rhs)
{
  size_t i;
  assert(width);

  busad $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), rhs, width, 1, 63, 0);
  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), lhs, width, 1, 63, 0);

  Emit(ctx, bus_add(ret, 0), bus_(SUPER, SUPER_ADD), 2);

  for (i = 1; i < width; i++) {
    busad $_1 = Label(ctx);
    Emit(ctx, bus_rel_fld($_1, +1, FLD_SRC, 0), bus_add(ret, i), 1);
    Emit(ctx, bus_add(ret, i), bus(SUPER), 2);
  }
}




/* -------------------------------------------------------------------------------------------------
   SUBTRACTION */




INITIALIZER(init_sub)
{
  for (uint16_t i = 0; i < 256; i++)
    for (uint16_t j = 0; j < 256; j++)
      for (uint16_t l = 0; l < 2; l++) {
        uint16_t r = i - j - l;
        SUPER[(i << 16 | j << 8 | SUPER_SUB) + 2 * l + 0] = r & 255;
        SUPER[(i << 16 | j << 8 | SUPER_SUB) + 2 * l + 1] = (r >> 8) ? SUPER_SUB + 2 : SUPER_SUB;
      }
}

void
Sub(emit_ctx* ctx, size_t width, busad ret, busad lhs, busad rhs)
{
  size_t i;
  assert(width);

  busad $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), rhs, width, 1, 63, 0);
  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), lhs, width, 1, 63, 0);

  Emit(ctx, bus_add(ret, 0), bus_(SUPER, SUPER_SUB), 2);

  for (i = 1; i < width; i++) {
    busad $_1 = Label(ctx);
    Emit(ctx, bus_rel_fld($_1, +1, FLD_SRC, 0), bus_add(ret, i), 1);
    Emit(ctx, bus_add(ret, i), bus(SUPER), 2);
  }
}




/* -------------------------------------------------------------------------------------------------
   BITWISE NOT */




[[gnu::used]] volatile uint8_t alignas(1 << 8) NOT[1 << 8];

INITIALIZER(init_not)
{
  for(uint16_t i = 0;i < 256;i++)
    NOT[i] = ~i;
}

void
Not(emit_ctx* ctx, size_t width, busad ret, busad inp)
{
  size_t i;
  assert(width);
  assert(width < 16);

  busad $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0, +1, FLD_SRC, 0), inp, width, 1, 31, 0);

  for (i = 0; i < width; i++)
    Emit(ctx, bus_add(ret, i), bus(NOT), 1);
}




/* -------------------------------------------------------------------------------------------------
   BITWISE AND */




INITIALIZER(init_and)
{
  for(uint16_t i = 0;i < 256;i++)
    for(uint16_t j = 0;j < 256;j++)
      SUPER[i << 16 | j << 8 | SUPER_AND] = i & j;
}

void
And(emit_ctx* ctx, size_t width, busad ret, busad lhs, busad rhs)
{
  size_t i;
  busad $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0,+2,FLD_SRC,1), lhs, width, 1, 31, 0);
  EmitStrided(ctx, bus_rel_fld($0,+2,FLD_SRC,2), rhs, width, 1, 31, 0);

  for(i = 0;i < width;i++)
    Emit(ctx, bus_add(ret, i), bus_(SUPER, SUPER_AND), 1);
}




/* -------------------------------------------------------------------------------------------------
   BITWISE OR */




INITIALIZER(init_or)
{
  for(uint16_t i = 0;i < 256;i++)
    for(uint16_t j = 0;j < 256;j++)
      SUPER[i << 16 | j << 8 | SUPER_OR] = i | j;
}

void
Or(emit_ctx* ctx, size_t width, busad ret, busad lhs, busad rhs)
{
  size_t i;
  busad $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0,+2,FLD_SRC,1), lhs, width, 1, 31, 0);
  EmitStrided(ctx, bus_rel_fld($0,+2,FLD_SRC,2), rhs, width, 1, 31, 0);

  for(i = 0;i < width;i++)
    Emit(ctx, bus_add(ret, i), bus_(SUPER, SUPER_OR), 1);
}




/* -------------------------------------------------------------------------------------------------
   BITWISE XOR */


  

INITIALIZER(init_xor)
{
  for(uint16_t i = 0;i < 256;i++)
    for(uint16_t j = 0;j < 256;j++)
      SUPER[i << 16 | j << 8 | SUPER_OR] = i | j;
}

void
Xor(emit_ctx* ctx, size_t width, busad ret, busad lhs, busad rhs)
{
  size_t i;
  busad $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0,+2,FLD_SRC,1), lhs, width, 1, 31, 0);
  EmitStrided(ctx, bus_rel_fld($0,+2,FLD_SRC,2), rhs, width, 1, 31, 0);

  for(i = 0;i < width;i++)
    Emit(ctx, bus_add(ret, i), bus_(SUPER, SUPER_XOR), 1);
}




/* -------------------------------------------------------------------------------------------------
   LEFT SHIFT LOGICAL */




INITIALIZER(init_sll) {
  uint32_t i, s;
  for(i=0;i<(1<<16);i++)
    for(s=0;s<8;s++) {
      SUPER[(i << 8 | SUPER_SLL) + s] = (i << s) >> 8;
    }
}

/* PRECONDITION: *shf < 8 */
void Sll8(emit_ctx *ctx, size_t width, busad ret, busad inp, busad shf)
{
  size_t i;
  busad $0;

  assert(width >= 2);

  $0 = Label(ctx);
  EmitStrided(ctx, bus_rel_fld($0,+4,FLD_SRC,1), inp, width, 2, 31, -1);
  EmitStridedWith(ctx, TI_DST_INC | TI_2DMODE, bus_rel_fld($0,+3,FLD_SRC,0), shf, width, 1, 31, 0);

  Emit(ctx, bus_rel_fld($0,+3,FLD_SRC,2), inp, 1);
  Emit(ctx, bus_add(ret,0), bus(SUPER), 1);
  Probe("$0+3:src", &Last(ctx)->src_addr, 4);
  for(i=1;i<width;i++) {
    Emit(ctx, bus_add(ret,i), bus(SUPER), 1);
    Probe("(iter):src", &Last(ctx)->src_addr, 4);
  }
}
