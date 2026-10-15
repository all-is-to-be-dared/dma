#include <bcm2835/dma/private.h>
#include <bcm2835/dma/emit.h>
#include <bcm2835/dma/arith.h>

#include <generic/math.h>




/* -------------------------------------------------------------------------------------------------
   INCREMENT */




INITIALIZER(increment)
{
  uint16_t i;

  for(i = 0;i < 256;i++) {
      SUPER[(i << 8 | SUPER_INC) + 0] = (i + 0) & 0xff;
      SUPER[(i << 8 | SUPER_INC) + 1] = (i + 0) >> 8 ? SUPER_INC + 2 : SUPER_INC;
      SUPER[(i << 8 | SUPER_INC) + 2] = (i + 1) & 0xff;
      SUPER[(i << 8 | SUPER_INC) + 3] = (i + 1) >> 8 ? SUPER_INC + 2 : SUPER_INC;
  }
}

void
Inc(emit_ctx * ctx, size_t width, busad ret, busad inp)
{
  size_t i;
  assert(width);

  busad $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0, +1, FLD_SRC, 1), inp, width, 1, 63, 0);

  for(i = 0;i < width;i++) {
    busad $_1 = Label(ctx);
    Emit(ctx, bus_add(ret, i), bus_(SUPER, SUPER_INC + 2), (i < width - 1) ? 2 : 1);
    if(i < width - 1)
      // Emit(ctx, bus_rel_fld($_1, +2, FLD_SRC, +0), bus_(SUPER, SUPER_INC + 1), 1);
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
Add(emit_ctx* ctx, size_t width, busad ret, busad lhs, busad rhs)
{
  size_t i;
  assert(width);

  busad $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), rhs, width, 1, 63, 0);
  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), lhs, width, 1, 63, 0);

  Emit(ctx, bus_add(ret, 0), bus_(HYPER, HYPER_ADD), 2);

  for (i = 1; i < width; i++) {
    busad $_1 = Label(ctx);
    Emit(ctx, bus_rel_fld($_1, +1, FLD_SRC, 0), bus_add(ret, i), 1);
    Emit(ctx, bus_add(ret, i), bus(HYPER), (i < width-1) ? 2 : 1);
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
Sub(emit_ctx* ctx, size_t width, busad ret, busad lhs, busad rhs)
{
  size_t i;
  assert(width);

  busad $0 = Label(ctx);

  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), rhs, width, 1, 63, 0);
  EmitStrided(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), lhs, width, 1, 63, 0);

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

  tmp = MonomorphizeOn(width, ({
    arena_alloc(&ARENA, width, min(min(width, 4), 16));
  }));

  Not(ctx, width, bus(tmp), inp);
  Inc(ctx, width, ret, bus(tmp));
}




/* -------------------------------------------------------------------------------------------------
   MULTIPLICATION */




INITIALIZER(mul)
{
  for(uint16_t b=0;b<256;b++)
    for(uint16_t c=0;c<256;c++) {
      HYPER[b<<16|c<<8|HYPER_MUL + 0] = (b * c) & 0xff;
      HYPER[b<<16|c<<8|HYPER_MUL + 1] = (b * c) >> 8;
    }
}

// An addition hypertable that reverses the usual order and stores (carry, sum) instead of
// (sum, carry). This is a necessary component of a fast multiplier.
INITIALIZER(add_r)
{
  for(uint16_t i=0;i<256;i++)
    for(uint16_t j=0;j<256;j++)
      for(uint16_t l=0;l<2;l++) {
        HYPER[i<<16|j<<8|HYPER_ADD_R|l<<1|0] = ((i + j + l) >> 8) ? HYPER_ADD_R + 2 : HYPER_ADD_R;
        HYPER[i<<16|j<<8|HYPER_ADD_R|l<<1|1] = (i + j + l) & 255;
      }
}

static inline
uint32_t log256(uint32_t x)
{
  uint32_t bits;
  bits = x ? 32 - __builtin_clz(x) : 0;
  return bits / 8;
}

// IMPORTANT:
//  - width of the output must be 2*width
void UmulWide(emit_ctx *ctx, size_t width, busad ret_w, busad lhs, busad rhs)
{
  // === Theoretical Basis
  // 
  // Given sum a_i L^i and sum b_i L^i, compute the product sum s_k L^k in the shortest time.
  // 
  // It turns out that standard grade-school multiplication is the best choice here for the simple
  // reason that it minimizes the number of multiplies. Therefore, the main direction in
  // optimization is simply to reduce the width of the operations as much as possible.
  //
  // Algorithm:
  //
  //  r_0 = a_0 b_0
  //  r_k = c_k + sum_(i+j=k) a_i b_j
  // 
  //  s_k = r_k (mod L)
  //  c_k = floor( r_k / L )
  //
  // To properly deal with operation widths, we can determine the maximum value of c_k at each step;
  // (a_i b_j) <= FE.01, so the maximum carry is bounded by
  //
  //  c_k ≤ floor( [ (2*min(k,W-1)-k+1) * FE.01 + C_(k-1) ] / L )
  //
  // Due to the nonlinear floor function, this is easier to carry out as a simple recurrence; this
  // is also amenable to a 'running sum'-style algorithm.
  //
  // === Optimization in terms of DMA blocks
  //
  //  1. Each r_k can minimize data movement using a pair of scatters togather the pairs.
  //  2. As mentioned previously, width of addition operations used in the multiplier carry can be
  //     restricted based on the maximum carry value at each step.

  assert(width);

  uint32_t C_k;

  uint16_t *small_products;

  small_products = MonomorphizeOn(width, ({
    arena_alloc(&ARENA, width * 2, 4);
  }));

  C_k = 0;

  for(int k = 0;k < 2 * width;k++) {
    // iterate over all i,j in [0,W) pairs such that i+j=k
    // start with j=min(k, W-1), end at j=k-min(k,W-1)
    // guaranteed that j≤W-1, j≤k, j≥0, and j≥k-W+1; since i=k-j, it follows that i≥0 and i≤k

    int i_min = k-min(k,width-1), i_max = min(k, width-1);
    unsigned scatter_count = i_max - i_min + 1;
    assert(scatter_count);

    busad $_1 = Label(ctx);
    EmitStrided(ctx, bus_rel_fld($_1, +2, FLD_SRC, 1), bus_add(lhs, i_min), scatter_count, 1, 31, 0);
    EmitStrided(ctx, bus_rel_fld($_1, +2, FLD_SRC, 2), bus_add(rhs, i_max), scatter_count, 1, 31, -2);
    
    // Calculate the small products a_i b_j

    for(int i = i_min;i < i_max;i++) {
      int j = k - i;
      assert(0 <= i && i < width && 0 <= j && j < width);

      Emit(ctx, bus_(small_products, i), bus_(HYPER, HYPER_MUL), 2);
    }

    // We sum the small_products, then write the result to bus_add(ret, k)
    // Small product sum is c_k + Σ a_i b_j.

    // C_k>0 for k>0
    if(C_k) {
      int LW = log256(C_k + 0xfe01);
      // if C_k is nonzero, then we're always writing to the operand of the following small product
      // summation
      busad $_Ck_add = Label(ctx);
      busad write_target = bus_rel_fld($_Ck_add, +2 * LW - 1, FLD_SRC, 0);
        
      Emit(ctx, bus_add(write_target, 0), bus_(HYPER, HYPER_ADD_R), 2);
      for(int l = 1;l < LW;l++) {
        busad $_Ck_add = Label(ctx);
        Emit(ctx, bus_rel_fld($_1, +1, FLD_SRC, 0), bus_rel_cb(write_target, l-1), 1);
        // Emit(ctx, bus_rel_fld($_Ck_add, +1, FLD_SRC, 0), bus_add(write_target, l), (l < LW - 1) ? 2 : 1);
      }
    }
  }
}
