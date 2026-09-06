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
} RTABI_SourceLocation;
static inline bool
sl_is_invalid(RTABI_SourceLocation *sl)
{
  return !sl->filename;
}
bool
sl_is_disabled(RTABI_SourceLocation *sl)
{
  return sl->col == (uint32_t)-1;
}

// -------------------------------------------------------------------------------------------------
// Value handles
typedef uintptr_t RTABI_ValueHandle;

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
} RTABI_TypeDescriptor;
static inline bool
td_is_int(RTABI_TypeDescriptor *td)
{
  return td->type_kind == TK_Integer || td->type_kind == TK_BitInt;
}

static inline bool
td_is_bitint(RTABI_TypeDescriptor *td)
{
  return td->type_kind == TK_BitInt;
}

bool
td_is_sbitint(RTABI_TypeDescriptor *td)
{
  return td_is_bitint(td) && (bool)(td->type_info & 1);
}
bool
td_is_sint(RTABI_TypeDescriptor *td)
{
  return td_is_bitint(td) && (bool)(td->type_info & 1);
}
bool
td_is_uint(RTABI_TypeDescriptor *td)
{
  return td_is_int(td) && !(td->type_info & 1);
}
uint32_t
td_get_bit_width(RTABI_TypeDescriptor *td)
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
  RTABI_SourceLocation loc;
  const RTABI_TypeDescriptor *ty;
  unsigned char log_alignment;
  enum type_check_kind type_check_kind;
} RTABI_TypeMismatchData;

/// \brief Handle a runtime type check failure, caused by either a misaligned
/// pointer, a null pointer, or a pointer to insufficient storage for the
/// type.
RECOVERABLE(type_mismatch_v1, RTABI_TypeMismatchData *Data, RTABI_ValueHandle Pointer);

typedef struct
{
  RTABI_SourceLocation loc;
  RTABI_SourceLocation assumption_loc;
  const RTABI_TypeDescriptor *ty;
} RTABI_AlignmentAssumptionData;

/// \brief Handle a runtime alignment assumption check failure,
/// caused by a misaligned pointer.
RECOVERABLE(alignment_assumption,
            RTABI_AlignmentAssumptionData *Data,
            RTABI_ValueHandle Pointer,
            RTABI_ValueHandle Alignment,
            RTABI_ValueHandle Offset);

typedef struct
{
  RTABI_SourceLocation loc;
  const RTABI_TypeDescriptor *ty;
} RTABI_OverflowData;

/// \brief Handle an integer addition overflow.
RECOVERABLE(add_overflow, RTABI_OverflowData *Data, RTABI_ValueHandle LHS, RTABI_ValueHandle RHS);

/// \brief Handle an integer subtraction overflow.
RECOVERABLE(sub_overflow, RTABI_OverflowData *Data, RTABI_ValueHandle LHS, RTABI_ValueHandle RHS);

/// \brief Handle an integer multiplication overflow.
RECOVERABLE(mul_overflow, RTABI_OverflowData *Data, RTABI_ValueHandle LHS, RTABI_ValueHandle RHS);

/// \brief Handle a signed integer overflow for a unary negate operator.
RECOVERABLE(negate_overflow, RTABI_OverflowData *Data, RTABI_ValueHandle OldVal);

/// \brief Handle an INT_MIN/-1 overflow or division by zero.
RECOVERABLE(divrem_overflow,
            RTABI_OverflowData *Data,
            RTABI_ValueHandle LHS,
            RTABI_ValueHandle RHS);

typedef struct
{
  RTABI_SourceLocation loc;
  const RTABI_TypeDescriptor *lhs_ty, *rhs_ty;
} RTABI_ShiftOutOfBoundsData;

/// \brief Handle a shift where the RHS is out of bounds or a left shift where
/// the LHS is negative or overflows.
RECOVERABLE(shift_out_of_bounds,
            RTABI_ShiftOutOfBoundsData *Data,
            RTABI_ValueHandle LHS,
            RTABI_ValueHandle RHS);

typedef struct
{
  RTABI_SourceLocation loc;
  const RTABI_TypeDescriptor *arr_ty, *idx_ty;
} RTABI_OutOfBoundsData;

/// \brief Handle an array index out of bounds error.
RECOVERABLE(out_of_bounds, RTABI_OutOfBoundsData *Data, RTABI_ValueHandle Index);

/// \brief Handle an local object access out of bounds error.
RECOVERABLE(local_out_of_bounds);

typedef struct
{
  RTABI_SourceLocation loc;
} RTABI_UnreachableData;

/// \brief Handle a __builtin_unreachable which is reached.
UNRECOVERABLE(builtin_unreachable, RTABI_UnreachableData *Data);
/// \brief Handle reaching the end of a value-returning function.
UNRECOVERABLE(missing_return, RTABI_UnreachableData *Data);

typedef struct
{
  RTABI_SourceLocation loc;
  const RTABI_TypeDescriptor *ty;
} RTABI_VLABoundData;

/// \brief Handle a VLA with a non-positive bound.
RECOVERABLE(vla_bound_not_positive, RTABI_VLABoundData *Data, RTABI_ValueHandle Bound);

typedef struct
{
  const RTABI_TypeDescriptor *from_ty, *to_ty;
} RTABI_FloatCastOverflowData;

typedef struct
{
  RTABI_SourceLocation loc;
  const RTABI_TypeDescriptor *from_ty, *to_ty;
} RTABI_FloatCastOverflowDataV2;

/// Handle overflow in a conversion to or from a floating-point type.
/// void *Data is one of FloatCastOverflowData* or FloatCastOverflowDataV2*
RECOVERABLE(float_cast_overflow, void *Data, RTABI_ValueHandle From);

typedef struct
{
  RTABI_SourceLocation loc;
  const RTABI_TypeDescriptor *ty;
} RTABI_InvalidValueData;

/// \brief Handle a load of an invalid value for the type.
RECOVERABLE(load_invalid_value, RTABI_InvalidValueData *Data, RTABI_ValueHandle Val);

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
  RTABI_SourceLocation loc;
  RTABI_TypeDescriptor *from_ty, *to_ty;
  enum implicit_conversion_check_kind kind;
  uint32_t bitfield_bits;
} RTABI_ImplicitConversionData;

/// \brief Implict conversion that changed the value.
RECOVERABLE(implicit_conversion,
            RTABI_ImplicitConversionData *Data,
            RTABI_ValueHandle Src,
            RTABI_ValueHandle Dst);

enum builtin_check_kind : uint8_t
{
  BCK_CTZPassedZero = 0,
  BCK_CLZPassedZero = 1,
  BCK_AssumePassedFalse = 2,
};
typedef struct
{
  RTABI_SourceLocation loc;
  enum builtin_check_kind kind;
} RTABI_InvalidBuiltinData;

/// Handle a builtin called in an invalid way.
RECOVERABLE(invalid_builtin, RTABI_InvalidBuiltinData *Data);

// LACUNA: InvalidObjCCast / RECOVERABLE(invalid_objc_cast)

typedef struct
{
  RTABI_SourceLocation loc;
} RTABI_NonNullReturnData;

/// \brief Handle returning null from function with the returns_nonnull
/// attribute, or a return type annotated with _Nonnull.
RECOVERABLE(nonnull_return_v1, RTABI_NonNullReturnData *Data, RTABI_SourceLocation *Loc);
RECOVERABLE(nullability_return_v1, RTABI_NonNullReturnData *Data, RTABI_SourceLocation *Loc);

typedef struct
{
  RTABI_SourceLocation loc, attr_loc;
  int32_t arg_idx;
} RTABI_NonNullArgData;

/// \brief Handle passing null pointer to a function parameter with the nonnull
/// attribute, or a _Nonnull type annotation.
RECOVERABLE(nonnull_arg, RTABI_NonNullArgData *Data);
RECOVERABLE(nullability_arg, RTABI_NonNullArgData *Data);

typedef struct
{
  RTABI_SourceLocation loc;
} RTABI_PointerOverflowData;

RECOVERABLE(pointer_overflow,
            RTABI_PointerOverflowData *Data,
            RTABI_ValueHandle Base,
            RTABI_ValueHandle Result);

// LACUNA: CFITypeCheckKind / CFICheckFailData / cfi_check_fail

typedef struct
{
  RTABI_SourceLocation loc;
  const RTABI_TypeDescriptor *ty;
} RTABI_FunctionTypeMismatchData;

// XXX: Not sure why by ubsan_handlers.h does this instead of using a RECOVERABLE()
//
// Looking at the code, it seems like the _abort() variant only aborts conditionally, but the
// condition is always (in the current code) true.
//
// Can probably switch __ubsan_handle_function_type_mismatch_abort() to [[noreturn]], honestly, or
// just use a normal RECOVERABLE().

SANITIZER_INTERFACE_ATTRIBUTE void
__ubsan_handle_function_type_mismatch(RTABI_FunctionTypeMismatchData *Data, RTABI_ValueHandle Val);
SANITIZER_INTERFACE_ATTRIBUTE void
__ubsan_handle_function_type_mismatch_abort(RTABI_FunctionTypeMismatchData *Data,
                                            RTABI_ValueHandle Val);
