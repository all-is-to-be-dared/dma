#include <bcm2835/dma/arith.h>

_TableSymbol static volatile alignas(256) uint8_t POLAR_TRUTH_X1[256];
_TableSymbol static volatile alignas(256) uint8_t ANTIPOLAR_TRUTH_X1[256];

_TableSymbol static volatile alignas(256) uint8_t POLAR_TRUTH_X4[256];
_TableSymbol static volatile alignas(256) uint8_t ANTIPOLAR_TRUTH_X4[256];

#define SET_TRUTH_TABLES(hyper, b0, b1)     \
  do {                                      \
    POLAR_TRUTH_X1[hyper + 0] = b0;         \
    POLAR_TRUTH_X1[hyper + 1] = b1;         \
    ANTIPOLAR_TRUTH_X1[hyper + 0] = b1;     \
    ANTIPOLAR_TRUTH_X1[hyper + 1] = b0;     \
    POLAR_TRUTH_X4[hyper + 0] = b0;         \
    POLAR_TRUTH_X4[hyper + 1] = 4 * b1;     \
    ANTIPOLAR_TRUTH_X4[hyper + 0] = 4 * b1; \
    ANTIPOLAR_TRUTH_X4[hyper + 1] = 4 * b0; \
  } while (0)




INITIALIZER(geu)
{
  for (uint16_t i = 0; i < 256; i++)
    for (uint16_t j = 0; j < 256; j++)
      for (size_t k = i * 256 + j, l = 0; l < 2; l++)
        HYPER[(k << 8) + HYPER_GEU + l] = ((i - j - l) >> 8) ? HYPER_GEU + 1 : HYPER_GEU;

  SET_TRUTH_TABLES(HYPER_GEU, 1, 0);
}

void
Geu(emit_ctx *ctx, size_t width, busad ret8, busad lhs, busad rhs, int flags)
{
  size_t i;
  volatile uint8_t *table;

  assert(width);

  busad $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), rhs, width, 1, 31, 0);
  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), lhs, width, 1, 31, 0);

  for (i = 0; i < width; i++) {
    busad $_1 = Label(ctx);
    Emit(
      ctx, bus_rel_fld($_1, +1, FLD_SRC, 0), bus_(HYPER, HYPER_GEU), 1);
  }

  table = (flags & ICF_X4) ? ((flags & ICF_INV) ? ANTIPOLAR_TRUTH_X4 : POLAR_TRUTH_X4)
                           : ((flags & ICF_INV) ? ANTIPOLAR_TRUTH_X1 : POLAR_TRUTH_X1);

  Emit(ctx, ret8, bus(table), 1);
}




INITIALIZER(equ)
{
  for(uint16_t i=0;i < 256;i++)
    for(uint16_t j=0;j<256;j++)
      for(size_t k = i * 256 + j, l = 0;l < 2;l++)
        HYPER[(k << 8) + HYPER_EQU + l] = ((i == j) && l) ? HYPER_EQU + 1 : HYPER_EQU;

  SET_TRUTH_TABLES(HYPER_EQU, 0, 1);
}

void
Equ(emit_ctx *ctx, size_t width, busad ret8, busad lhs, busad rhs, int flags)
{
  size_t i;
  volatile uint8_t *table;

  assert(width);

  busad $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), rhs, width, 1, 31, 0);
  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), lhs, width, 1, 31, 0);

  for (i = 0; i < width; i++) {
    busad $_1 = Label(ctx);
    Emit(
      ctx, bus_rel_fld($_1, +1, FLD_SRC, 0), bus_(HYPER, HYPER_EQU + 1), 1);
  }

  table = (flags & ICF_X4) ? ((flags & ICF_INV) ? ANTIPOLAR_TRUTH_X4 : POLAR_TRUTH_X4)
                           : ((flags & ICF_INV) ? ANTIPOLAR_TRUTH_X1 : POLAR_TRUTH_X1);

  Emit(ctx, ret8, bus(table), 1);
}




INITIALIZER(ges)
{
  enum {
    GES_HIGH = HYPER_GES + 0,
    GES_FALSE = HYPER_GES + 1,
    GES_TRUE = HYPER_GES + 2,
    GES_EQU = HYPER_GES + 3,
  };
  for(uint16_t i = 0;i < 256;i++)
    for(uint16_t j = 0;j < 256;j++)
      for(size_t k = i * 256 + j, l = 0; l < 4;l++) {
        int8_t si = i, sj = j;
        uint8_t next;
        switch(HYPER_GES + l) {
          case GES_HIGH: next = si < sj ? GES_FALSE : si > sj ? GES_TRUE : GES_EQU; break;
          case GES_EQU: next = i < j ? GES_FALSE : i > j ? GES_TRUE : GES_EQU; break;
          case GES_FALSE:
          case GES_TRUE: next = HYPER_GES + l; break;
          default: __builtin_unreachable();
        }
        HYPER[(k << 8) + HYPER_GES + l] = next;
      }

  POLAR_TRUTH_X1[GES_HIGH] = POLAR_TRUTH_X1[GES_FALSE] = 0;
  POLAR_TRUTH_X1[GES_TRUE] = POLAR_TRUTH_X1[GES_EQU] = 1;

  ANTIPOLAR_TRUTH_X1[GES_HIGH] = ANTIPOLAR_TRUTH_X1[GES_FALSE] = 1;
  ANTIPOLAR_TRUTH_X1[GES_TRUE] = ANTIPOLAR_TRUTH_X1[GES_EQU] = 0;

  POLAR_TRUTH_X4[GES_HIGH] = POLAR_TRUTH_X4[GES_FALSE] = 0;
  POLAR_TRUTH_X4[GES_TRUE] = POLAR_TRUTH_X4[GES_EQU] = 4;

  ANTIPOLAR_TRUTH_X4[GES_HIGH] = ANTIPOLAR_TRUTH_X4[GES_FALSE] = 4;
  ANTIPOLAR_TRUTH_X4[GES_TRUE] = ANTIPOLAR_TRUTH_X4[GES_EQU] = 0;
}

void
Ges(emit_ctx *ctx, size_t width, busad ret8, busad lhs, busad rhs, int flags)
{
  size_t i;
  volatile uint8_t *table;
  int32_t next;

  assert(width);

  busad $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), bus_add(rhs, 0), width, 1, 31, 0);
  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), bus_add(lhs, 0), width, 1, 31, 0);

  Branch(ctx, bus_rel_cb($0, width + 1));

  for (i = 0; i < width; i++) {
    busad $_1 = Label(ctx);
    next = i ? -1 : +width;
    Emit(
      ctx, bus_rel_fld($_1, next, FLD_SRC, 0), bus_(HYPER, HYPER_GES), 1);
    Branch(ctx, bus_rel_cb($_1, next));
  }

  table = (flags & ICF_X4) ? ((flags & ICF_INV) ? ANTIPOLAR_TRUTH_X4 : POLAR_TRUTH_X4)
                           : ((flags & ICF_INV) ? ANTIPOLAR_TRUTH_X1 : POLAR_TRUTH_X1);

  Emit(ctx, ret8, bus(table), 1);
}
