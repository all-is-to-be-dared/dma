#include <bcm2835/dma/private.h>
#include <bcm2835/dma/emit.h>
#include <bcm2835/dma/arith.h>




/* -------------------------------------------------------------------------------------------------
   LEFT SHIFT LOGICAL */




_TableSymbol
static volatile uint8_t alignas(256) SLL_HYPER_SELECT[256];

INITIALIZER(sll)
{
  uint32_t i, s;
  for (i = 0; i < (1 << 16); i++)
    for (s = 0; s < 8; s++)
      HYPER[(i << 8 | HYPER_SLL) + s] = (i << s) >> 8;
  for (i = 0; i < 256; i++)
    SLL_HYPER_SELECT[i] = HYPER_SLL + (i & 7);
}

void
Sll8(emit_ctx* ctx, size_t width, busad ret, busad inp, busad shf8)
{
  size_t i;
  busad $0;

  assert(width >= 2);

  // dd cc bb aa
  //  |/ |/ |/ |
  // DD CC BB AA

  $0 = Label(ctx);
  EmitStrided(ctx, bus_rel_fld($0, +5, FLD_SRC, 1), inp, width - 1, 2, 30, -1);
  // SLL_HYPER_SELECT != 0
  Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 0), shf8, 1);
  // SRC_INC|2DMODE works as expected _if_ cols=1 like we have here
  EmitStridedWith(ctx,
                  TI_DST_INC | TI_2DMODE,
                  bus_rel_fld($0, +4, FLD_SRC, 0),
                  bus(SLL_HYPER_SELECT),
                  width,
                  1,
                  31,
                  0);
  Emit(ctx, bus_rel_fld($0, +4, FLD_SRC, 2), inp, 1);
  Emit(ctx, bus_add(ret, 0), bus(HYPER), 1);
  for (i = 1; i < width; i++)
    Emit(ctx, bus_add(ret, i), bus(HYPER), 1);
}

void
Sll(emit_ctx* c, size_t width, busad ret, busad inp, busad shf8)
{
  busad $0;
  volatile uint8_t *SLL_SHIFT_SELECT, *tmp;

  assert(width < 128);
  SLL_SHIFT_SELECT = MonomorphizeOn(width, ({
                            uint8_t* t = arena_alloc(&ARENA, 256, 256);
                            for (int i = 0; i < 256; i++)
                              t[i] = width - ((i / 8) % width);
                            t;
                          }));
  tmp = MonomorphizeOn(width, ({ arena_alloc(&ARENA, 2 * width, 256); }));

  Emit(c, bus(tmp), bus(ZERO), width);
  Sll8(c, width, bus_(tmp, width), inp, shf8);

  $0 = Label(c);
  Emit(c, bus_rel_fld($0, +1, FLD_SRC, 0), shf8, 1);
  Emit(c, bus_rel_fld($0, +2, FLD_SRC, 0), bus(SLL_SHIFT_SELECT), 1);
  Emit(c, ret, bus(tmp), width);
}




/* -------------------------------------------------------------------------------------------------
   RIGHT SHIFT LOGICAL */




_TableSymbol static volatile uint8_t alignas(256) SRL_HYPER_SELECT[256];

INITIALIZER(init_srl)
{
  uint32_t i, s;
  for (i = 0; i < (1 << 16); i++)
    for (s = 0; s < 8; s++)
      HYPER[(i << 8 | HYPER_SRL) + s] = i >> s;
  for (i = 0; i < 256; i++) {
    SRL_HYPER_SELECT[i] = HYPER_SRL + (i & 7);
  }
}

void
Srl8(emit_ctx* ctx, size_t width, busad ret, busad inp, busad shf8)
{
  size_t i;
  busad $0;

  assert(width >= 2);

  // dd cc bb aa
  // | \| \| \| 
  // DD CC BB AA

  $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0,+5,FLD_SRC,1), inp, width-1, 2, 30, -1);
  Emit(ctx, bus_rel_fld($0,+2,FLD_SRC,0), shf8, 1);
  EmitStridedWith(ctx, TI_DST_INC|TI_2DMODE, bus_rel_fld($0,+4,FLD_SRC,0), bus(SRL_HYPER_SELECT),
                  width,1,31,0);

  Emit(ctx, bus_rel_fld($0,+4,FLD_SRC,1), bus_add(inp,width-1), 1);
  Emit(ctx, bus_add(ret,width-1), bus(HYPER), 1);
  for (i = 0; i < width - 1; i++)
    Emit(ctx, bus_add(ret, i), bus(HYPER), 1);
}

void
Srl(emit_ctx* c, size_t width, busad ret, busad inp, busad shf8)
{
  busad $0;
  uint8_t *SRL_SHIFT_SELECT, *tmp;

  assert(width < 128);
  SRL_SHIFT_SELECT = MonomorphizeOn(width, ({
                            uint8_t* t = arena_alloc(&ARENA, 256, 256);
                            for (int i = 0; i < 256; i++)
                              t[i] = (i / 8) % width;
                            t;
                          }));
  tmp = MonomorphizeOn(width, ({ arena_alloc(&ARENA, 2 * width, 256); }));


  // TODO: can I drop this line?
  Emit(c, bus(tmp), bus(ZERO), width);
  Srl8(c, width, bus_(tmp, 0), inp, shf8);

  $0 = Label(c);
  Emit(c, bus_rel_fld($0, +1, FLD_SRC, 0), shf8, 1);
  Emit(c, bus_rel_fld($0, +2, FLD_SRC, 0), bus(SRL_SHIFT_SELECT), 1);
  Emit(c, ret, bus(tmp), width);
}




/* -------------------------------------------------------------------------------------------------
   RIGHT SHIFT ARITHMETIC */



_TableSymbol static volatile uint8_t alignas(1<<8) SEXT_WHICH[1<<8];
_TableSymbol static volatile uint8_t alignas(1<<5) SEXT_BUFS[1<<5];

INITIALIZER(init_sext)
{
  uint32_t i;
  for(i=0;i<(1<<8);i++)
    SEXT_WHICH[i] = (i & 0x80) ? (uintptr_t)(SEXT_BUFS+16) & 255 : (uintptr_t)(SEXT_BUFS) & 255;
  for(i=0;i<32;i++)
    SEXT_BUFS[i] = (i < 16) ? 0 : 0xff;
}

_TableSymbol static volatile uint8_t alignas(1<<16) SRA_HI[1<<16];

INITIALIZER(init_sra)
{
  uint32_t i, s;
  for(i=0;i<(1<<8);i++)
    for(s=0;s<(1<<8);s++)
      SRA_HI[i<<8|s] = ((int8_t)i) >> (s % 8);
}

void
Sra8(emit_ctx *c, size_t width, busad ret, busad inp, busad shf8)
{
  busad $0;
  size_t i;

  assert(width >= 2);

  // dd cc bb aa
  // $ \| \| \|
  // DD CC BB AA
  //
  // DD uses the SRA_HI table
  
  $0 = Label(c);

  EmitStrided(c, bus_rel_fld($0,+5,FLD_SRC,1), inp,width-1, 2, 30, -1);
  EmitStridedWith(c, TI_DST_INC|TI_2DMODE, bus_rel_fld($0,+2,FLD_SRC,0), shf8, 2, 1, 63, 0);
  EmitStridedWith(c, TI_DST_INC|TI_2DMODE, bus_rel_fld($0,+5,FLD_SRC,0), bus(SRL_HYPER_SELECT),
                  width-1,1,31,0); // +2

  Emit(c, bus_rel_fld($0,+4,FLD_SRC,1), bus_add(inp,width-1), 1);
  Emit(c, bus_add(ret,width-1), bus(SRA_HI), 1);
  for (i = 0; i < width - 1; i++)
    Emit(c, bus_add(ret, i), bus(HYPER), 1);
}

void
Sra(emit_ctx *c, size_t width, busad ret, busad inp, busad shf8)
{
  busad $0, $1;
  uint8_t *SRA_SHIFT_SELECT, *tmp;

  assert(width < 128, "due to monotable alignment, etc.");
  // TODO: This MAY have a bug where width>16 breaks it (see: $0+2)
  
  // actually the same as SRL_SHIFT SELECT; should merge the two
  SRA_SHIFT_SELECT = MonomorphizeOn(width, ({
                                      uint8_t *t = arena_alloc(&ARENA, 256, 256);
                                      for(int i = 0;i < 256;i++)
                                        t[i] = (i / 8) % width;
                                      t;
                                    }));
  // should merge with SLL/SRL
  tmp = MonomorphizeOn(width, ({ arena_alloc(&ARENA, 2 * width, 256); }));

  $0 = Label(c);
  // TODO: check if it's faster to do this in 2dmode
  Emit(c, bus_rel_fld($0,+1,FLD_SRC,0), bus_add(inp,width-1), 1);
  Emit(c, bus_rel_fld($0,+2,FLD_SRC,0), bus(SEXT_WHICH), 1);
  EmitWith(c, TI_DST_INC, bus_(tmp, width), bus(SEXT_BUFS), width);

  Sra8(c, width, bus(tmp), inp, shf8);
  
  $1 = Label(c);
  Emit(c, bus_rel_fld($1, +1, FLD_SRC, 0), shf8, 1);
  Emit(c, bus_rel_fld($1, +2, FLD_SRC, 0), bus(SRA_SHIFT_SELECT), 1);
  Emit(c, ret, bus(tmp), width);
}
