#pragma once

#include <string.h>
#include <bcm2835-dma/private.h>

/* -------------------------------------------------------------------------------------------------
   BUS ADDRESS MANIPULATION */

enum cb_field
{
  FLD_SRC,
  FLD_DST,
  FLD_LEN,
  FLD_NXT,
};

static const inline uint32_t
cb_field_offset(enum cb_field f)
{
  switch (f) {
    case FLD_SRC:
      return __builtin_offsetof(cblk, src_addr);
    case FLD_DST:
      return __builtin_offsetof(cblk, dst_addr);
    case FLD_LEN:
      return __builtin_offsetof(cblk, txfr_len);
    case FLD_NXT:
      return __builtin_offsetof(cblk, next_cb);
    default:
      panic("cb_field_offset: invalid field %d\n", f);
  }
}

// Convert ARM pointer to bus_t.
static const inline busad
bus(volatile void* ptr)
{
  return arm_to_bus((uintptr_t)ptr);
}
// Convert ARM pointer to bus_t, and add byte_offset.
static const inline busad
bus_(volatile void* ptr, int32_t byte_offset)
{
  return arm_to_bus((uintptr_t)ptr + byte_offset);
}

// Add `byte_offset` to `base`.
static const inline busad
bus_add(busad base, int32_t byte_offset)
{
  return (busad){ ._0 = base._0 + byte_offset };
}

// Add `cb_offset` control block lengths to `base`.
//
// For example:
// ```
// cb_t blocks[2];
// bus_t base = bus(&blocks[0]->TXFR_LEN);
// bus_t addr = bus_rel_cb(base,+1);
// ```
// addr will be the 0th byte of the length field of the 2nd block in `blocks`.
static const inline busad
bus_rel_cb(busad base, int32_t cb_offset)
{
  return bus_add(base, cb_offset * sizeof(cblk));
}

// The address of the specified `field` (plus `offset_in_field`) in the control block `cb_offset`
// control blocks away from `base`.
//
// For example:
// ```
// cb_t blocks[2];
// bus_t base = bus(blocks);
// bus_t addr = bus_rel_fld(base,+1,FLD_SRC,+1);
// ```
// addr will be the +1th byte of the source address field of the 2nd block in `blocks`.
static const inline busad
bus_rel_fld(busad base, int32_t cb_offset, enum cb_field field, uint32_t offset_in_field)
{
  // this operation only makes sense when base is a control block.
  assert(base._0 % 32 == 0);
  // fields in the control block are only 4 bytes
  assert(offset_in_field < 4);
  return bus_add(base, cb_offset * sizeof(cblk) + cb_field_offset(field) + offset_in_field);
}

// Check if two bus addresses are equal.
static const inline bool
bus_eq(busad a, busad b)
{
  return a._0 == b._0;
}

// Get the null bus address (literal 0), needed for setting NEXT_CB=0.
static const inline busad
bus_null(void)
{
  return (busad){ 0 };
}




/* -------------------------------------------------------------------------------------------------
   CONTROL BLOCK EMISSION */



typedef struct
{
  cblk *base, *p;
  size_t cap;
} emit_ctx;

emit_ctx
mk_emit_ctx(size_t cap);




static inline cblk*
Here(emit_ctx* ctx)
{
  return ctx->p;
}
static inline cblk*
Last(emit_ctx* ctx)
{
  assert(ctx->p > ctx->base);
  return ctx->p - 1;
}
static inline busad
Label(emit_ctx* ctx)
{
  return bus(ctx->p);
}
static inline bool
At(emit_ctx* ctx, busad b)
{
  return bus_eq(Label(ctx), b);
}
static inline void
Branch(emit_ctx* ctx, busad label)
{
  Last(ctx)->next_cb = label;
}
static inline void
End(emit_ctx* ctx)
{
  Last(ctx)->next_cb = bus_null();
}




static inline void
EmitStridedWith(emit_ctx* ctx,
                uint32_t flags,
                busad dst,
                busad src,
                uint16_t rows,
                uint16_t cols,
                uint16_t dst_stride,
                uint16_t src_stride)
{
  cblk* cb;

  if ((ctx->p - ctx->base) >= ctx->cap)
    panic("EmitStridedWith: ran out of blocks (limit set by mk_emit_ctx: %d)\n", ctx->cap);

  assert(flags & TI_2DMODE);

  cb = ctx->p++;

  memset(cb, 0, sizeof *cb);
  cb->ti = flags;
  cb->dst_addr = dst;
  cb->src_addr = src;
  cb->txfr_len = length(rows - 1, cols);
  cb->stride = stride(dst_stride, src_stride);
  cb->next_cb = bus(ctx->p);
}

static inline void
EmitStrided(emit_ctx* ctx,
            busad dst,
            busad src,
            uint16_t rows,
            uint16_t cols,
            uint16_t dst_stride,
            uint16_t src_stride)
{
  EmitStridedWith(
    ctx, TI_DST_INC | TI_SRC_INC | TI_2DMODE, dst, src, rows, cols, dst_stride, src_stride);
}




static inline void
EmitWith(emit_ctx* ctx, uint32_t flags, busad dst, busad src, size_t len)
{
  cblk* cb;

  if ((ctx->p - ctx->base) >= ctx->cap)
    panic("EmitWith: ran out of blocks (limit set by mk_emit_ctx: %d)\n", ctx->cap);

  cb = ctx->p++;

  memset(cb, 0, sizeof *cb);
  cb->ti = flags;
  cb->dst_addr = dst;
  cb->src_addr = src;
  cb->txfr_len = len;
  cb->next_cb = bus(ctx->p);
}

static inline void
Emit(emit_ctx* ctx, busad dst, busad src, size_t len)
{
  EmitWith(ctx, TI_DST_INC | TI_SRC_INC, dst, src, len);
}




static inline void
SkipToAlign(emit_ctx *ctx, size_t align, bool update_last)
{
  uintptr_t curr;
  size_t next_aligned, delta;
  cblk *new_p;

  assert(align >= sizeof(cblk), "alignment must be at least that of a control block\n");
  assert(!(align % alignof(cblk)), "alignment must a multiple of control block alignment\n");
  assert((align & (align - 1)) == 0, "alignment %d is not a power of 2\n", align);

  curr = (uintptr_t)ctx->p;
  next_aligned = (curr + (align - 1)) & ~(align - 1);
  delta = next_aligned - curr;
  new_p = ctx->p + (delta / sizeof(cblk));

  if((new_p - ctx->base) >= ctx->cap)
    panic("SkipToAlign: ran out of blocks (limit set by mk_emit_ctx: %d)\n", ctx->cap);
  if(update_last)
    Branch(ctx, bus(new_p));

  ctx->p = new_p;
}




/* -------------------------------------------------------------------------------------------------
   BUFFER ALLOCATION */




typedef struct {
  void *start, *end, *p;
  size_t util;
} arena;

static inline arena mk_arena(void *start, void *end)
{
  assert(start >= end, "Arena cannot end before it begins");
  return (arena) { start, end, start, 0 };
}

static inline void *arena_alloc(arena *a, size_t size, size_t align)
{
  uintptr_t q, r;

  assert(size <= (SIZE_MAX / 2), "allocation must be no larger than 0x%zx bytes\n", (SIZE_MAX/2));
  assert((align & (align - 1)) == 0, "alignment %d must be a power of 2\n", align);

  q = (uintptr_t)a->p;
  q = (q + align - 1) & (-align);
  r = q + size;

  if(r >= (uintptr_t)a->end || r < (uintptr_t)a->start)
    panic("arena_alloc: arena [%p,%p) ran out of space: allocating %zu/%zu on %p would overflow\n",
      a->start, a->end, size, align, a->p);

  a->p = (void*)r;
  a->util += size;

  return (void*)q;
}

typedef struct {
  size_t cap, util, rem;
} arena_stats;
static inline arena_stats
arena_get_stats(arena *a)
{
  arena_stats s;

  s.cap = a->end - a->start;
  s.rem = a->end - a->p;
  s.util = a->util;

  return s;
}

static inline void arena_reset(arena *a)
{
  a->p = a->start;
}

extern arena ARENA;
