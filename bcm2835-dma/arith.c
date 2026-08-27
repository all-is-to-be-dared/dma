#include "bcm2835/platform.h"
#include <stdint.h>
#include <string.h>

#include <bcm2835-dma/private.h>
#include <bcm2835-dma/emit.h>
#include <bcm2835-dma/arith.h>

#include <printf/printf.h>




uint8_t alignas(256) ZERO[256] = {};

[[gnu::used]] volatile uint8_t alignas(1 << 24) SUPER[1 << 24];

enum
{
  SUPER_ADD = 0x04, // 4
  SUPER_SUB = 0x08, // 4
  SUPER_AND = 0x0c, // 1
  SUPER_OR = 0x0d,  // 1
  SUPER_XOR = 0x0e, // 1

  SUPER_SLL = 0x14, // 8

  SUPER_SRL = 0x24, // 8
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
  for (uint16_t i = 0; i < 256; i++)
    NOT[i] = ~i;
}

void
Not(emit_ctx* ctx, size_t width, busad ret, busad inp)
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




INITIALIZER(init_and)
{
  for (uint16_t i = 0; i < 256; i++)
    for (uint16_t j = 0; j < 256; j++)
      SUPER[i << 16 | j << 8 | SUPER_AND] = i & j;
}

void
And(emit_ctx* ctx, size_t width, busad ret, busad lhs, busad rhs)
{
  size_t i;
  busad $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), lhs, width, 1, 31, 0);
  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), rhs, width, 1, 31, 0);

  for (i = 0; i < width; i++)
    Emit(ctx, bus_add(ret, i), bus_(SUPER, SUPER_AND), 1);
}




/* -------------------------------------------------------------------------------------------------
   BITWISE OR */




INITIALIZER(init_or)
{
  for (uint16_t i = 0; i < 256; i++)
    for (uint16_t j = 0; j < 256; j++)
      SUPER[i << 16 | j << 8 | SUPER_OR] = i | j;
}

void
Or(emit_ctx* ctx, size_t width, busad ret, busad lhs, busad rhs)
{
  size_t i;
  busad $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), lhs, width, 1, 31, 0);
  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), rhs, width, 1, 31, 0);

  for (i = 0; i < width; i++)
    Emit(ctx, bus_add(ret, i), bus_(SUPER, SUPER_OR), 1);
}




/* -------------------------------------------------------------------------------------------------
   BITWISE XOR */




INITIALIZER(init_xor)
{
  for (uint16_t i = 0; i < 256; i++)
    for (uint16_t j = 0; j < 256; j++)
      SUPER[i << 16 | j << 8 | SUPER_OR] = i | j;
}

void
Xor(emit_ctx* ctx, size_t width, busad ret, busad lhs, busad rhs)
{
  size_t i;
  busad $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), lhs, width, 1, 31, 0);
  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), rhs, width, 1, 31, 0);

  for (i = 0; i < width; i++)
    Emit(ctx, bus_add(ret, i), bus_(SUPER, SUPER_XOR), 1);
}




/* -------------------------------------------------------------------------------------------------
   LEFT SHIFT LOGICAL */




static uint8_t alignas(256) SLL_SUPER_SELECT[256];

INITIALIZER(init_sll)
{
  uint32_t i, s;
  for (i = 0; i < (1 << 16); i++)
    for (s = 0; s < 8; s++)
      SUPER[(i << 8 | SUPER_SLL) + s] = (i << s) >> 8;
  for (i = 0; i < 256; i++)
    SLL_SUPER_SELECT[i] = SUPER_SLL + (i & 7);
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
  // SLL_SUPER_SELECT != 0
  Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 0), shf8, 1);
  // SRC_INC|2DMODE works as expected _if_ cols=1 like we have here
  EmitStridedWith(ctx,
                  TI_DST_INC | TI_2DMODE,
                  bus_rel_fld($0, +4, FLD_SRC, 0),
                  bus(SLL_SUPER_SELECT),
                  width,
                  1,
                  31,
                  0);
  Emit(ctx, bus_rel_fld($0, +4, FLD_SRC, 2), inp, 1);
  Emit(ctx, bus_add(ret, 0), bus(SUPER), 1);
  for (i = 1; i < width; i++)
    Emit(ctx, bus_add(ret, i), bus(SUPER), 1);
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




static uint8_t alignas(256) SRL_SUPER_SELECT[256];

INITIALIZER(init_srl)
{
  uint32_t i, s;
  for (i = 0; i < (1 << 16); i++)
    for (s = 0; s < 8; s++)
      SUPER[(i << 8 | SUPER_SRL) + s] = i >> s;
  for (i = 0; i < 256; i++) {
    SRL_SUPER_SELECT[i] = SUPER_SRL + (i & 7);
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
  EmitStridedWith(ctx, TI_DST_INC|TI_2DMODE, bus_rel_fld($0,+4,FLD_SRC,0), bus(SRL_SUPER_SELECT),
                  width,1,31,0);

  Emit(ctx, bus_rel_fld($0,+4,FLD_SRC,1), bus_add(inp,width-1), 1);
  Emit(ctx, bus_add(ret,width-1), bus(SUPER), 1);
  for (i = 0; i < width - 1; i++)
    Emit(ctx, bus_add(ret, i), bus(SUPER), 1);
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


  Emit(c, bus(tmp), bus(ZERO), width);
  Srl8(c, width, bus_(tmp, 0), inp, shf8);

  $0 = Label(c);
  Emit(c, bus_rel_fld($0, +1, FLD_SRC, 0), shf8, 1);
  Emit(c, bus_rel_fld($0, +2, FLD_SRC, 0), bus(SRL_SHIFT_SELECT), 1);
  Emit(c, ret, bus(tmp), width);
}




/* -------------------------------------------------------------------------------------------------
   RIGHT SHIFT ARITHMETIC */



#define _TableSymbol [[gnu::section(".dma.tables"), gnu::used]]

_TableSymbol static volatile uint8_t alignas(1<<8) SEXT_WHICH[1<<8];
_TableSymbol static volatile uint8_t alignas(1<<5) SEXT_BUFS[32];

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

  EmitStrided(c, bus_rel_fld($0,+5,FLD_SRC,1), inp, width-1, 2, 30, -1);
  EmitStridedWith(c, TI_DST_INC|TI_2DMODE, bus_rel_fld($0,+2,FLD_SRC,0), shf8, 2, 1, 63, 0);
  EmitStridedWith(c, TI_DST_INC|TI_2DMODE, bus_rel_fld($0,+5,FLD_SRC,0), bus(SRL_SUPER_SELECT),
                  width,1,31,0);

  Emit(c, bus_rel_fld($0,+4,FLD_SRC,1), bus_add(inp,width-1), 1);
  Emit(c, bus_add(ret,width-1), bus(SRA_HI), 1);
  for (i = 0; i < width - 1; i++)
    Emit(c, bus_add(ret, i), bus(SUPER), 1);
}

uint32_t sp_save = 0;

void
Sra(emit_ctx *c, size_t width, busad ret, busad inp, busad shf8)
{
  // uint32_t f, s;
  // asm volatile("mov %0, r11\n\tmov %1, r13":"=r"(f),"=r"(s));
  // printf("(Sra1) fp=%p sp=%p\n", f, s);
  
  busad $0, $1;
  uint8_t *SRA_SHIFT_SELECT, *tmp;

  assert(width < 128);
  assert(width < 16, ".. due to the size of SEXT_BUFS");
  // actually the same as SRL_SHIFT SELECT; should merge the two
  SRA_SHIFT_SELECT = MonomorphizeOn(width, ({
                                      uint8_t *t = arena_alloc(&ARENA, 256, 256);
                                      for(int i = 0;i < 256;i++)
                                        t[i] = (i / 8) % width;
                                      t;
                                    }));
  // should merge with SLL/SRL
  tmp = MonomorphizeOn(width, ({ arena_alloc(&ARENA, 2 * width, 256); }));

  // Probe("tmp", tmp, width*2);

  $0 = Label(c);
  // TODO: check if it's faster to do this in 2dmode
  Emit(c, bus_rel_fld($0,+1,FLD_SRC,0), shf8, 1);
  Emit(c, bus_rel_fld($0,+2,FLD_SRC,0), bus(SEXT_WHICH), 1);
  EmitWith(c, TI_DST_INC, bus_(tmp, width), bus(SEXT_BUFS), width);

  Sra8(c, width, bus_(tmp, 0), inp, shf8);
  // Sra8(c, width, ret, inp, shf8);
  
  $1 = Label(c);
  Emit(c, bus_rel_fld($1, +1, FLD_SRC, 0), shf8, 1);
  Emit(c, bus_rel_fld($1, +2, FLD_SRC, 0), bus(SRA_SHIFT_SELECT), 1);
  // // Probe("Sra.sra_shift_select!", &Last(c)->src_addr, 4);
  Emit(c, ret, bus(tmp), width);
  // // Probe("tmp_select!", &Last(c)->src_addr, 4);

  // aux_uart_can_read();
  // uint32_t sp;
  // asm volatile("mov %0, r13":"=r"(sp));
  // asm volatile("" :/*outputs*/ :/*inputs*/ :/*clobbers*/"sp");
  // sp_save = sp;
  // printf("r13=%p\n", sp);
}
