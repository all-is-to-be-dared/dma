#include "bcm2835/dma/private.h"
#include <bcm2835/dma/arith.h>

#include <generic/math.h>




INITIALIZER(4div)
{
  for(uint16_t i = 0;i < 256;i++)
    SUPER[i<<8|SUPER_4DIV_X4] = (i % 4) == 0 ? 0 : 4;
}




INITIALIZER(mul)
{
  for (uint16_t b = 0; b < 256; b++)
    for (uint16_t c = 0; c < 256; c++) {
      HYPER[b << 16 | c << 8 | HYPER_MUL + 0] = (b * c) & 0xff;
      HYPER[b << 16 | c << 8 | HYPER_MUL + 1] = (b * c) >> 8;
    }
}

enum
{
  SCA_MULTIPLY = +2,
  SCA_ADD = +1,
};

typedef uint64_t inter_t;

static inline size_t
log256u(inter_t x)
{
  if (!x)
    return 0;
  int y = sizeof(inter_t) * 8 - __builtin_clzg(x);
  return (y + 7) / 8;
}

// Symmetric (NxN) Widening (->2N) Unsigned Multiplication.
void
UMulW(emit_ctx *ctx, size_t width, busad ret_w, busad lhs, busad rhs)
{
  assert(width);

  {
    busad a = bus_add(lhs, 0), b = bus_add(rhs, 0), r = bus_add(ret_w, 0);

    // Always runs at full width, because ret_w is always twice width, so 1x1->2 always fits.
    busad $0 = Label(ctx);
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), a, 1);
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), b, 1);
    Emit(ctx, r, bus_(HYPER, HYPER_MUL), 2);
  }

  // What is the maximum possible intermediate value that needs to go through the next adder?
  inter_t intermax;
  // At the moment, it's 0xFE because 0xFF*0xFF=0xFE.01
  intermax = 0xFE;

  // there are 2N-1 output digits that are not produced by carry.
  const size_t penultimate_layer = 2 * width - 2;

  // Loop across output digits
  for (size_t layer = 1; layer < penultimate_layer; layer++) {
    // const size_t target_count = layer > width ? 2 * width - layer : layer;

    // start with aN, bM where
    // N = max(0, I-W + 1)
    // M = min(I, W-1)
    const size_t a_off = (size_t)max((ssize_t)0, (ssize_t)layer - (ssize_t)width + 1),
                 b_off = min(layer, width - 1);
    const busad a = bus_add(lhs, a_off);
    const busad b = bus_add(rhs, b_off);
    const size_t layer_multiplies = abs(b_off - a_off + 1);
    cblk *writeforward_targets[layer_multiplies];

    // Emit 1x1->2 small multiplications in the layer
    {
      busad $layer_mul = Label(ctx);
      EmitStrided(ctx, bus_rel_fld($layer_mul, +2, FLD_SRC, 1), a, layer_multiplies, 1, +31, +0);
      EmitStrided(ctx, bus_rel_fld($layer_mul, +2, FLD_SRC, 2), b, layer_multiplies, 1, +31, -2);
      for (size_t i = 0; i < layer_multiplies; i++)
        writeforward_targets[i] =
          EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0);
    }

    // need width-1 intermediates
    // maximum intermediate width: MAXCRY + FE01 * (width - 1)
    // return width: MAXCRY + FE01 * width

    size_t theoretical_interwidth = log256u(intermax + 0xFE01 * (layer_multiplies - 1));
    size_t actual_interwidth = min(theoretical_interwidth, 2 * width - layer);


    uint8_t *inter =
      MonomorphizeOn(layer_multiplies << 16 | layer, arena_alloc(&ARENA, actual_interwidth, 4));
    // char probe_name_buf[32];
    // snprintf(probe_name_buf, sizeof probe_name_buf, "o_%d", layer);
    // Probe(probe_name_buf, inter, actual_interwidth);

    for (size_t i = 0; i < layer_multiplies; i++) {
      // Calculate the maximum bytewidth of the intermediate before this adder.
      // Note that this can differ from the addition width.
      const size_t carry_load_width = log256u(intermax);

      // Maximum value in the layer's accumulator after adding the current 1x1->2 result.
      //
      // From this, we calculate the minimum required width of the addition.
      intermax += 0xFE01;
      const size_t addition_width = min(log256u(intermax), 2 * width - layer);

      // We need to figure out where the intermediate *is*; it might be the cross-layer
      // intermediate stored in ret_w, or it might be the intra-layer intermediate in `inter`.
      const busad adder_src = i ? bus(inter) : bus_add(ret_w, layer);
      // If the adder is the last one in the layer, then it needs to write back to ret_w.
      const busad adder_dst = i == (layer_multiplies - 1) ? bus_add(ret_w, layer) : bus(inter);

      // Now emit the actual adder; fairly simple ripple-carry structure.
      busad $add = Label(ctx);
      if (carry_load_width > 1)
        EmitStrided(
          ctx, bus_rel_fld($add, +1, FLD_SRC, +SCA_ADD), adder_src, carry_load_width, 1, +63, +0);
      else
        Emit(ctx, bus_rel_fld($add, +1, FLD_SRC, +SCA_ADD), adder_src, 1);
      writeforward_targets[i]->dst_addr = bus_rel_fld($add, +1, FLD_SRC, +SCA_MULTIPLY);
      for (size_t j = 0; j < addition_width; j++) {
        busad $ripple_carry = Label(ctx);
        bool last_rca = j == (addition_width - 1);
        Emit(ctx, bus_add(adder_dst, j), bus_(HYPER, HYPER_ADD), !last_rca ? 2 : 1);
        if (!last_rca)
          Emit(ctx, bus_rel_fld($ripple_carry, +2, FLD_SRC, 0), bus_add(adder_dst, j + 1), 1);
      }
    }

    intermax /= 256;
  }

  // Final layer is fairly simple; 2-wide addition

  {
    cblk *writeforward_target;
    {
      busad $final = Label(ctx);
      Emit(ctx, bus_rel_fld($final, +2, FLD_SRC, 1), bus_add(lhs, width - 1), 1);
      Emit(ctx, bus_rel_fld($final, +2, FLD_SRC, 2), bus_add(rhs, width - 1), 1);
      writeforward_target = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0);
    }

    {
      busad $add = Label(ctx);
      EmitStrided(ctx,
                  bus_rel_fld($add, +1, FLD_SRC, +SCA_ADD),
                  bus_add(ret_w, 2 * width - 2),
                  2,
                  1,
                  +63,
                  +0);
      writeforward_target->dst_addr = bus_rel_fld($add, +1, FLD_SRC, +SCA_MULTIPLY);
      Emit(ctx, bus_add(ret_w, 2 * width - 2), bus_(HYPER, HYPER_ADD), 2);
      Emit(ctx, bus_rel_fld($add, +3, FLD_SRC, 0), bus_add(ret_w, 2 * width - 1), 1);
      Emit(ctx, bus_add(ret_w, 2 * width - 1), bus_(HYPER, HYPER_ADD), 1);
    }
  }
}

void
UMul(emit_ctx *ctx, size_t width, busad ret, busad lhs, busad rhs)
{
  assert(width);

  {
    // If width == 1, then use half width. Otherwise, use full width.
    busad $0 = Label(ctx);
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 1), lhs, 1);
    Emit(ctx, bus_rel_fld($0, +2, FLD_SRC, 2), rhs, 1);
    Emit(ctx, ret, bus_(HYPER, HYPER_MUL), width > 1 ? 2 : 1);
  }

  // If width == 1, then just return now. There's nothing else we can do here.
  if(width < 2)
    return;

  inter_t intermax;

  intermax = 0xFE;

  // N-1 output digits that are not produced by carry. Last layer is sufficiently similar that we
  // can just include it in the primary layer generation loop.
  const size_t final_layer = width;

  for (size_t layer = 1; layer < final_layer; layer++) {
    const size_t a_off = (size_t)max((ssize_t)0, (ssize_t)layer - (ssize_t)width + 1),
                 b_off = min(layer, width - 1);
    const busad a = bus_add(lhs, a_off);
    const busad b = bus_add(rhs, b_off);
    const size_t layer_multiplies = abs(b_off - a_off + 1);
    
    cblk *writeforward_targets[layer_multiplies];

    {
      busad $layer_mul = Label(ctx);
      EmitStrided(ctx, bus_rel_fld($layer_mul, +2, FLD_SRC, 1), a, layer_multiplies, 1, +31, +0);
      EmitStrided(ctx, bus_rel_fld($layer_mul, +2, FLD_SRC, 2), b, layer_multiplies, 1, +31, -2);
      if(layer < (final_layer - 1)) {
        for(size_t i = 0;i < layer_multiplies;i++)
          writeforward_targets[i] = EmitStrided(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 2, 1, +63, +0);
      } else {
        for(size_t i = 0;i < layer_multiplies;i++)
          writeforward_targets[i] = Emit(ctx, bus_null(), bus_(HYPER, HYPER_MUL), 1);
      }
    }

    const size_t theoretical_interwidth = log256u(intermax + 0xFE01 * (layer_multiplies - 1));
    const size_t actual_interwidth = min(theoretical_interwidth, width - layer);

    uint8_t *inter = MonomorphizeOn(layer_multiplies << 16 | layer, arena_alloc(&ARENA, actual_interwidth, 4));

    for(size_t i = 0;i < layer_multiplies;i++) {
      const size_t carry_load_width = min(log256u(intermax), width - layer);

      assert(carry_load_width);

      intermax += 0xFE01;
      const size_t addition_width = min(log256u(intermax), width - layer);
      const busad adder_src = i ? bus(inter) : bus_add(ret, layer);
      const busad adder_dst = i == (layer_multiplies - 1) ? bus_add(ret, layer) : bus(inter);

      busad $add = Label(ctx);
      if(carry_load_width > 1)
        EmitStrided(ctx, bus_rel_fld($add, +1, FLD_SRC, +SCA_ADD), adder_src, carry_load_width, 1, +63, +0);
      else
        Emit(ctx, bus_rel_fld($add, +1, FLD_SRC, SCA_ADD), adder_src, 1);
      writeforward_targets[i]->dst_addr = bus_rel_fld($add, +1, FLD_SRC, +SCA_MULTIPLY);
      for(size_t j = 0;j < addition_width;j++) {
        busad $ripple_carry = Label(ctx);
        bool last_rca = j == (addition_width - 1);
        Emit(ctx, bus_add(adder_dst, j), bus_(HYPER, HYPER_ADD), !last_rca ? 2 : 1);
        if (!last_rca)
          Emit(ctx, bus_rel_fld($ripple_carry, +2, FLD_SRC, 0), bus_add(adder_dst, j+1), 1);
      }
    }

    intermax /= 256;
  }
}
