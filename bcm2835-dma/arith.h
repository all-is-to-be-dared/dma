#include <stdint.h>
#include <bcm2835-dma/private.h>
#include <bcm2835-dma/emit.h>

void
Add(emit_ctx* ctx, size_t width, busad ret, busad lhs, busad rhs);

void
Sub(emit_ctx* ctx, size_t width, busad ret, busad lhs, busad rhs);

void
Not(emit_ctx* ctx, size_t width, busad ret, busad inp);

void
And(emit_ctx* ctx, size_t width, busad ret, busad lhs, busad rhs);

void
Or(emit_ctx* ctx, size_t width, busad ret, busad lhs, busad rhs);

void
Xor(emit_ctx* ctx, size_t width, busad ret, busad lhs, busad rhs);

void
Sll8(emit_ctx *ctx, size_t width, busad ret, busad inp, busad shf);
