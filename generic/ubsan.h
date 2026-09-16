#pragma once

// -------------------------------------------------------------------------------------------------
// UBSAN CORE

#include <generic/assert.h>
#include <string.h>
#include <stdint.h>

// -------------------------------------------------------------------------------------------------
// Source locations
typedef struct
{
  const char *filename;
  uint32_t line;
  uint32_t col;
} SourceLocation;
static inline bool
sl_is_invalid(SourceLocation *sl)
{
  return !sl->filename;
}
static inline bool
sl_is_disabled(SourceLocation *sl)
{
  return sl->col == (uint32_t)-1;
}

// -------------------------------------------------------------------------------------------------
// Value handles
typedef uintptr_t ValueHandle;

// -------------------------------------------------------------------------------------------------
// Type descriptors
enum type_kind : uint16_t
{
  // Integer type. Interpretation of type_info is:
  //  - bit 0 is 1 for signed, 0 otherwise
  //  - bits 15:1 are log2(bit width)
  // Value representation is raw integer if it fits in ValueHandle, pointer to
  // integer otherwise
  TK_Integer = 0x0000,
  // Floating-point type. Interpretation of type_info is:
  //  - bits 15:0 are bit width
  // Value representation is that of bitcasting the floating-point value to an
  // integer type
  TK_Float = 0x0001,
  // A _BitInt(N) type. Interpretation of type_info is:
  //  - bit 0 is 1 for signed, 0 for unsigned
  //  - bits 15:1 are log2(bit width).
  // Value representation is raw integer if it fits in ValueHandle, pointer to
  // integer otherwise. `type_name` contains the true width of the type for the
  // signed _BitInt(N) type stored after zero byte after TypeName as 32-bit
  // unsigned integer (?)
  TK_BitInt = 0x0002,
  // Unknown. Value representation unspecified.
  TK_Unknown = 0xffff,
};
typedef struct
{
  enum type_kind type_kind;
  uint16_t type_info;
  char type_name[1];
} TypeDescriptor;
static inline bool
td_is_int(const TypeDescriptor *td)
{
  return td->type_kind == TK_Integer || td->type_kind == TK_BitInt;
}

static inline bool
td_is_bitint(const TypeDescriptor *td)
{
  return td->type_kind == TK_BitInt;
}

static inline bool
td_is_sbitint(const TypeDescriptor *td)
{
  return td_is_bitint(td) && (bool)(td->type_info & 1);
}
static inline bool
td_is_sint(const TypeDescriptor *td)
{
  return td_is_int(td) && (bool)(td->type_info & 1);
}
static inline bool
td_is_uint(const TypeDescriptor *td)
{
  return td_is_int(td) && !(td->type_info & 1);
}
static inline uint32_t
td_get_bit_width(const TypeDescriptor *td)
{
  uint32_t r;
  if (td_is_sbitint(td)) {
    const char *p = td->type_name;
    while (*p)
      p++;
    memcpy(&r, p + 1, 4);
  } else {
    r = 1 << (td->type_info >> 1);
  }
  return r;
}
static inline bool
td_is_float(const TypeDescriptor *td)
{
  return td->type_kind == TK_Float;
}

#define SANITIZER_INTERFACE_ATTRIBUTE [[gnu::visibility("default")]]

#define UNRECOVERABLE(checkname, ...)        \
  SANITIZER_INTERFACE_ATTRIBUTE [[noreturn]] \
  void __ubsan_handle_##checkname(__VA_ARGS__)

#define RECOVERABLE(checkname, ...)             \
  SANITIZER_INTERFACE_ATTRIBUTE                 \
  void __ubsan_handle_##checkname(__VA_ARGS__); \
  SANITIZER_INTERFACE_ATTRIBUTE [[noreturn]]    \
  void __ubsan_handle_##checkname##_abort(__VA_ARGS__)

enum type_check_kind : uint8_t
{
  /// Checking the operand of a load. Must be suitably sized and aligned.
  TCK_Load,
  /// Checking the destination of a store. Must be suitably sized and aligned.
  TCK_Store,
  /// Checking the bound value in a reference binding. Must be suitably sized
  /// and aligned, but is not required to refer to an object (until the
  /// reference is used), per core issue 453.
  TCK_ReferenceBinding,
  /// Checking the object expression in a non-static data member access. Must
  /// be an object within its lifetime.
  TCK_MemberAccess,
  /// Checking the 'this' pointer for a call to a non-static member function.
  /// Must be an object within its lifetime.
  TCK_MemberCall,
  /// Checking the 'this' pointer for a constructor call.
  TCK_ConstructorCall,
  /// Checking the operand of a static_cast to a derived pointer type. Must be
  /// null or an object within its lifetime.
  TCK_DowncastPointer,
  /// Checking the operand of a static_cast to a derived reference type. Must
  /// be an object within its lifetime.
  TCK_DowncastReference,
  /// Checking the operand of a cast to a base object. Must be suitably sized
  /// and aligned.
  TCK_Upcast,
  /// Checking the operand of a cast to a virtual base object. Must be an
  /// object within its lifetime.
  TCK_UpcastToVirtualBase,
  /// Checking the value assigned to a _Nonnull pointer. M[<43;74;7
  /// null.
  TCK_NonnullAssign,
  /// Checking the operand of a dynamic_cast or a typeid expression.  Must be
  /// null or an object within its lifetime.
  TCK_DynamicOperation
};
typedef struct
{
  SourceLocation loc;
  const TypeDescriptor *ty;
  unsigned char log_alignment;
  enum type_check_kind type_check_kind;
} TypeMismatchData;

/// \brief Handle a runtime type check failure, caused by either a misaligned
/// pointer, a null pointer, or a pointer to insufficient storage for the
/// type.
RECOVERABLE(type_mismatch_v1, TypeMismatchData *Data, ValueHandle Pointer);

typedef struct
{
  SourceLocation loc;
  SourceLocation assumption_loc;
  const TypeDescriptor *ty;
} AlignmentAssumptionData;

/// \brief Handle a runtime alignment assumption check failure,
/// caused by a misaligned pointer.
RECOVERABLE(alignment_assumption,
            AlignmentAssumptionData *Data,
            ValueHandle Pointer,
            ValueHandle Alignment,
            ValueHandle Offset);

typedef struct
{
  SourceLocation loc;
  const TypeDescriptor *ty;
} OverflowData;

/// \brief Handle an integer addition overflow.
RECOVERABLE(add_overflow, OverflowData *Data, ValueHandle LHS, ValueHandle RHS);

/// \brief Handle an integer subtraction overflow.
RECOVERABLE(sub_overflow, OverflowData *Data, ValueHandle LHS, ValueHandle RHS);

/// \brief Handle an integer multiplication overflow.
RECOVERABLE(mul_overflow, OverflowData *Data, ValueHandle LHS, ValueHandle RHS);

/// \brief Handle a signed integer overflow for a unary negate operator.
RECOVERABLE(negate_overflow, OverflowData *Data, ValueHandle OldVal);

/// \brief Handle an INT_MIN/-1 overflow or division by zero.
RECOVERABLE(divrem_overflow,
            OverflowData *Data,
            ValueHandle LHS,
            ValueHandle RHS);

typedef struct
{
  SourceLocation loc;
  const TypeDescriptor *lhs_ty, *rhs_ty;
} ShiftOutOfBoundsData;

/// \brief Handle a shift where the RHS is out of bounds or a left shift where
/// the LHS is negative or overflows.
RECOVERABLE(shift_out_of_bounds,
            ShiftOutOfBoundsData *Data,
            ValueHandle LHS,
            ValueHandle RHS);

typedef struct
{
  SourceLocation loc;
  const TypeDescriptor *arr_ty, *idx_ty;
} OutOfBoundsData;

/// \brief Handle an array index out of bounds error.
RECOVERABLE(out_of_bounds, OutOfBoundsData *Data, ValueHandle Index);

/// \brief Handle an local object access out of bounds error.
RECOVERABLE(local_out_of_bounds);

typedef struct
{
  SourceLocation loc;
} UnreachableData;

/// \brief Handle a __builtin_unreachable which is reached.
UNRECOVERABLE(builtin_unreachable, UnreachableData *Data);
/// \brief Handle reaching the end of a value-returning function.
UNRECOVERABLE(missing_return, UnreachableData *Data);

typedef struct
{
  SourceLocation loc;
  const TypeDescriptor *ty;
} VLABoundData;

/// \brief Handle a VLA with a non-positive bound.
RECOVERABLE(vla_bound_not_positive, VLABoundData *Data, ValueHandle Bound);

typedef struct
{
  const TypeDescriptor *from_ty, *to_ty;
} FloatCastOverflowData;

typedef struct
{
  SourceLocation loc;
  const TypeDescriptor *from_ty, *to_ty;
} FloatCastOverflowDataV2;

/// Handle overflow in a conversion to or from a floating-point type.
/// void *Data is one of FloatCastOverflowData* or FloatCastOverflowDataV2*
RECOVERABLE(float_cast_overflow, void *Data, ValueHandle From);

typedef struct
{
  SourceLocation loc;
  const TypeDescriptor *ty;
} InvalidValueData;

/// \brief Handle a load of an invalid value for the type.
RECOVERABLE(load_invalid_value, InvalidValueData *Data, ValueHandle Val);

enum implicit_conversion_check_kind : uint8_t
{
  ICCK_IntegerTruncation = 0, // Legacy (only clang 7)
  ICCK_UnsignedIntegerTruncation = 1,
  ICCK_SignedIntegerTruncation = 2,
  ICCK_IntegerSignChange = 3,
  ICCK_SignedIntegerTruncationOrSignChange = 4,
};
typedef struct
{
  SourceLocation loc;
  TypeDescriptor *from_ty, *to_ty;
  enum implicit_conversion_check_kind kind;
  uint32_t bitfield_bits;
} ImplicitConversionData;

/// \brief Implicit conversion that changed the value.
RECOVERABLE(implicit_conversion,
            ImplicitConversionData *Data,
            ValueHandle Src,
            ValueHandle Dst);

enum builtin_check_kind : uint8_t
{
  BCK_CTZPassedZero = 0,
  BCK_CLZPassedZero = 1,
  BCK_AssumePassedFalse = 2,
};
typedef struct
{
  SourceLocation loc;
  enum builtin_check_kind kind;
} InvalidBuiltinData;

/// Handle a builtin called in an invalid way.
RECOVERABLE(invalid_builtin, InvalidBuiltinData *Data);

// LACUNA: InvalidObjCCast / RECOVERABLE(invalid_objc_cast)

typedef struct
{
  SourceLocation loc;
} NonNullReturnData;

/// \brief Handle returning null from function with the returns_nonnull
/// attribute, or a return type annotated with _Nonnull.
RECOVERABLE(nonnull_return_v1, NonNullReturnData *Data, SourceLocation *Loc);
RECOVERABLE(nullability_return_v1, NonNullReturnData *Data, SourceLocation *Loc);

typedef struct
{
  SourceLocation loc, attr_loc;
  int32_t arg_idx;
} NonNullArgData;

/// \brief Handle passing null pointer to a function parameter with the nonnull
/// attribute, or a _Nonnull type annotation.
RECOVERABLE(nonnull_arg, NonNullArgData *Data);
RECOVERABLE(nullability_arg, NonNullArgData *Data);

typedef struct
{
  SourceLocation loc;
} PointerOverflowData;

RECOVERABLE(pointer_overflow,
            PointerOverflowData *Data,
            ValueHandle Base,
            ValueHandle Result);

// LACUNA: CFITypeCheckKind / CFICheckFailData / cfi_check_fail

typedef struct
{
  SourceLocation loc;
  const TypeDescriptor *ty;
} FunctionTypeMismatchData;

// XXX: Not sure why by ubsan_handlers.h does this instead of using a RECOVERABLE()
//
// Looking at the code, it seems like the _abort() variant only aborts conditionally, but the
// condition is always (in the current code) true.
//
// Can probably switch __ubsan_handle_function_type_mismatch_abort() to [[noreturn]], honestly, or
// just use a normal RECOVERABLE().

SANITIZER_INTERFACE_ATTRIBUTE void
__ubsan_handle_function_type_mismatch(FunctionTypeMismatchData *Data, ValueHandle Val);
SANITIZER_INTERFACE_ATTRIBUTE void
__ubsan_handle_function_type_mismatch_abort(FunctionTypeMismatchData *Data,
                                            ValueHandle Val);




uint32_t ubsan_report_count(void);
