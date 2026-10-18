#pragma once

#include <stdint.h>

#include <bcm2835/dma/private.h>
#include <bcm2835/dma/emit.h>




#define _TableSymbol [[gnu::section(".dma.tables"), gnu::used]]

extern volatile uint8_t alignas(256) ZERO[256];
extern volatile uint8_t alignas(1 << 24) HYPER[1 << 24];
extern volatile uint8_t alignas(1 << 16) SUPER[1 << 16];

enum
{
  HYPER_ADD = 0x04, // 4
  HYPER_SUB = 0x08, // 4
  HYPER_AND = 0x0c, // 1
  HYPER_OR = 0x0d,  // 1
  HYPER_XOR = 0x0e, // 1
  // available: 0f

  HYPER_SLL = 0x14, // 8
  // available: 1c-1f

  HYPER_SRL = 0x24, // 8

  HYPER_GEU = 0x2c, // 2
  HYPER_EQU = 0x2e, // 2

  HYPER_GES = 0x34, // 4

  HYPER_MUL = 0x38, // 2
  // available: 3a-3f

  // available: 44-47
  HYPER_ADD_R = 0x48, // 8

  // available: 50 onwards, except x0-x3
};
enum {
  SUPER_INC = 0x00, // 4
};





// -------------------------------------------------------------------------------------------------
// Integer Arithmetic Operations

void
Inc(emit_ctx* ctx, size_t width, busad ret, busad inp);

void
Add(emit_ctx* ctx, size_t width, busad ret, busad lhs, busad rhs);

void
Sub(emit_ctx* ctx, size_t width, busad ret, busad lhs, busad rhs);

void
Neg(emit_ctx *ctx, size_t width, busad ret, busad inp);

void
Mul(emit_ctx *ctx, size_t width, busad ret_wide, busad lhs, busad rhs);

void
Udv(emit_ctx *ctx, size_t width, busad quot, busad rem, busad num, busad den);

void
Sdv(emit_ctx *ctx, size_t width, busad quot, busad rem, busad num, busad den);

// -------------------------------------------------------------------------------------------------
// Integer Bitwise Operations

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

void
Sll(emit_ctx *ctx, size_t width, busad ret, busad inp, busad shf);

void
Srl8(emit_ctx *ctx, size_t width, busad ret, busad inp, busad shf);

void
Srl(emit_ctx *ctx, size_t width, busad ret, busad inp, busad shf);

void
Sra8(emit_ctx *ctx, size_t width, busad ret, busad inp, busad shf);

void
Sra(emit_ctx *ctx, size_t width, busad ret, busad inp, busad shf);

// -------------------------------------------------------------------------------------------------
// Integer Comparison Operations

enum {
  // While one often desires T=1, F=0, in this line of work it is also often the case that one wants
  // T=4, F=0 for various reasons.
  ICF_X4 = 1 << 0,

  // GEU(INV;) is LTU(;). Enough said.
  ICF_INV = 1 << 1,
};

void
Geu(emit_ctx *ctx, size_t width, busad ret8, busad lhs, busad rhs, int flags);

inline void
Ltu(emit_ctx *ctx, size_t width, busad ret8, busad lhs, busad rhs, int flags) {
  Geu(ctx, width, ret8, lhs, rhs, flags ^ ICF_INV);
}

void
Equ(emit_ctx *ctx, size_t width, busad ret8, busad lhs, busad rhs, int flags);

inline void
Neq(emit_ctx *ctx, size_t width, busad ret8, busad lhs, busad rhs, int flags)
{
  Equ(ctx, width, ret8, lhs, rhs, flags ^ ICF_INV);
}

void
Ges(emit_ctx *ctx, size_t width, busad ret8, busad lhs, busad rhs, int flags);

inline void
Lts(emit_ctx *ctx, size_t width, busad ret8, busad lhs, busad rhs, int flags)
{
  Ges(ctx, width, ret8, lhs, rhs, flags ^ ICF_INV);
}

// -------------------------------------------------------------------------------------------------
//
