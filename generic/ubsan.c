#include <generic/ubsan.h>
#include <generic/printf.h>

[[noreturn]]
static void
ubsan_abort()
{
  panic("ubsan: unrecoverable fault\n");
}




[[noreturn]]
void
abort()
{
  panic("abort() called by libunwind?\n");
}




// -------------------------------------------------------------------------------------------------
// TYPE MISMATCH




const char *const TYPE_CHECK_KIND_NAMES[] = {
  [TCK_Load] = "load of",
  [TCK_Store] = "store to",
  [TCK_ReferenceBinding] = "reference binding to",
  [TCK_MemberAccess] = "member access within",
  [TCK_MemberCall] = "member call on",
  [TCK_ConstructorCall] = "constructor call on",
  [TCK_DowncastPointer] = "downcast of",
  [TCK_DowncastReference] = "downcast of",
  [TCK_Upcast] = "upcast of",
  [TCK_UpcastToVirtualBase] = "cast to virtual base of",
  [TCK_NonnullAssign] = "_Nonnull binding to",
  [TCK_DynamicOperation] = "dynamic operation on",
};

enum error_type
{
  ET_GenericUB,
  ET_NullPointerUse,
  ET_NullPointerUseWithNullability,
  ET_NullptrWithOffset,
  ET_NullptrWithNonZeroOffset,
  ET_NullptrAfterNonZeroOffset,
  ET_PointerOverflow,
  ET_MisalignedPointerUse,
  ET_AlignmentAssumption,
  ET_InsufficientObjectSize,
  ET_SignedIntegerOverflow,
  ET_UnsignedIntegerOverflow,
  ET_IntegerDivideByZero,
  ET_FloatDivideByZero,
  ET_InvalidBuiltin,
  // InvalidObjCCast,
  ET_ImplicitUnsignedIntegerTruncation,
  ET_ImplicitSignedIntegerTruncation,
  ET_ImplicitSignedIntegerSignChange,
  ET_ImplicitSignedIntegerTruncationOrSignChange,
  ET_InvalidShiftBase,
  ET_InvalidShiftExponent,
  ET_OutOfBoundsIndex,
  ET_LocalOutOfBounds,
  ET_UnreachableCall,
  ET_MissingReturn,
  ET_NonPositiveVLAIndex,
  ET_FloatCastOverflow,
  ET_InvalidBoolLoad,
  ET_InvalidEnumLoad,
  ET_FunctionTypeMismatch,
  ET_InvalidNullReturn,
  ET_InvalidNullReturnWithNullability,
  ET_InvalidNullArgument,
  ET_InvalidNullArgumentWithNullability,
  ET_DynamicTypeMismatch,
  // CFIVCall, CFINVCall, CFIDerivedCast, CFIUnrelatedCast, CFIICall, CFIMCall
};

static const char *error_type_summaries[] = {
  "undefined-behavior",
  "null-pointer-use",
  "null-pointer-use",
  "nullptr-with-offset",
  "nullptr-with-nonzero-offset",
  "nullptr-after-nonzero-offset",
  "pointer-overflow",
  "misaligned-pointer-use",
  "alignment-assumption",
  "insufficient-object-size",
  "signed-integer-overflow",
  "unsigned-integer-overflow",
  "integer-divide-by-zero",
  "float-divide-by-zero",
  "invalid-builtin-use",
  // "invalid-objc-cast",
  "implicit-unsigned-integer-truncation",
  "implicit-signed-integer-truncation",
  "implicit-signed-integer-sign-change",
  "implicit-signed-integer-truncation-or-sign-change",
  "invalid-shift-base",
  "invalid-shift-exponent",
  "out-of-bounds-index",
  "local-out-of-bounds",
  "unreachable-call",
  "missing-return",
  "non-positive-vla-index",
  "float-cast-overflow",
  "invalid-bool-load",
  "invalid-enum-load",
  "function-type-mismatch",
  "invalid-null-return",
  "invalid-null-return",
  "invalid-null-argument",
  "invalid-null-argument",
  "dynamic-type-mismatch",
  // CFIVCall, CFINVCall, CFIDerivedCast, CFIUnrelatedCast, CFIICall, CFIMCall
};

#define PRE "\x1b[31m\x1b[1m"
#define NUM "\x1b[35m"
#define RST "\x1b[0m"

struct ubsan_report_opts
{
  uintptr_t pc, bp;
  bool unrecoverable;
};
#define UBSAN_GET_RETURN_ADDR(level) __builtin_extract_return_addr(__builtin_return_address(level))
#define UBSAN_GET_FRAME_ADDR(level) __builtin_frame_address(level)
#define UBSAN_OPTS(etc)                                                  \
  (struct ubsan_report_opts){ .pc = (uintptr_t)UBSAN_GET_RETURN_ADDR(0), \
                              .bp = (uintptr_t)UBSAN_GET_FRAME_ADDR(0),  \
                              etc };

static int ubsan_error_index = 0;

static void
ubsan_report_begin(RTABI_SourceLocation *loc, enum error_type et, struct ubsan_report_opts opts)
{
  printf("\n============================== [UBSAN REPORT %d] ==============================\n",
         ubsan_error_index);
  printf("%s:%u:%u: %s: ", loc->filename, loc->line, loc->col, error_type_summaries[et]);
}
static void
ubsan_report_end(struct ubsan_report_opts opts)
{
  printf("bp=%p lr=%p\n", opts.bp, opts.pc);
  printf("============================== [ END REPORT %d ] ==============================\n",
         ubsan_error_index);
  ubsan_error_index++;
}

static void
handle_type_mismatch(RTABI_TypeMismatchData *Data,
                     RTABI_ValueHandle Pointer,
                     struct ubsan_report_opts opts)
{
  uintptr_t alignment;
  enum error_type et;

  alignment = (uintptr_t)1 << Data->log_alignment;
  if (!Pointer) {
    et = (Data->type_check_kind == TCK_NonnullAssign) ? ET_NullPointerUseWithNullability
                                                      : ET_NullPointerUse;
  } else if (Pointer & (alignment - 1)) {
    et = ET_MisalignedPointerUse;
  } else {
    et = ET_InsufficientObjectSize;
  }

  ubsan_report_begin(&Data->loc, et, opts);

  switch (et) {
    case ET_NullPointerUse:
    case ET_NullPointerUseWithNullability:
      printf("%s null pointer of type %s\n",
             TYPE_CHECK_KIND_NAMES[Data->type_check_kind],
             Data->ty->type_name);
      break;
    case ET_MisalignedPointerUse:
      printf("%s misaligned address %p for type %s which requires %d byte alignment\n",
             TYPE_CHECK_KIND_NAMES[Data->type_check_kind],
             Pointer,
             Data->ty->type_name,
             alignment);
      break;
    case ET_InsufficientObjectSize:
      printf("%s address %p with insufficient space for an object of type %s\n",
             TYPE_CHECK_KIND_NAMES[Data->type_check_kind],
             Pointer,
             Data->ty->type_name);
      break;
    default:
      panic("unreachable!\n");
  }

  if (Pointer)
    printf("NOTE: %p: pointer points to here\n", Pointer);

  ubsan_report_end(opts);
}

void
__ubsan_handle_type_mismatch_v1(RTABI_TypeMismatchData *Data, RTABI_ValueHandle Pointer)
{
  struct ubsan_report_opts opts = UBSAN_OPTS(.unrecoverable = false);
  handle_type_mismatch(Data, Pointer, opts);
}
void
__ubsan_handle_type_mismatch_v1_abort(RTABI_TypeMismatchData *Data, RTABI_ValueHandle Pointer)
{
  struct ubsan_report_opts opts = UBSAN_OPTS(.unrecoverable = false);
  handle_type_mismatch(Data, Pointer, opts);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// ALIGNMENT ASSUMPTION




static void
handle_alignment_assumption(RTABI_AlignmentAssumptionData *Data,
                            RTABI_ValueHandle Pointer,
                            RTABI_ValueHandle Alignment,
                            RTABI_ValueHandle Offset)
{
  todo("implement me\n");
}

void
__ubsan_handle_alignment_assumption(RTABI_AlignmentAssumptionData *Data,
                                    RTABI_ValueHandle Pointer,
                                    RTABI_ValueHandle Alignment,
                                    RTABI_ValueHandle Offset)
{
  handle_alignment_assumption(Data, Pointer, Alignment, Offset);
}
void
__ubsan_handle_alignment_assumption_abort(RTABI_AlignmentAssumptionData *Data,
                                          RTABI_ValueHandle Pointer,
                                          RTABI_ValueHandle Alignment,
                                          RTABI_ValueHandle Offset)
{
  handle_alignment_assumption(Data, Pointer, Alignment, Offset);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// INTEGER OVERFLOW




static void
handle_integer_overflow(RTABI_OverflowData *Data,
                        RTABI_ValueHandle LHS,
                        RTABI_ValueHandle RHS,
                        const char *symbol)
{
  todo("implement me!\n");
}

void
__ubsan_handle_add_overflow(RTABI_OverflowData *Data, RTABI_ValueHandle LHS, RTABI_ValueHandle RHS)
{
  handle_integer_overflow(Data, LHS, RHS, "+");
}
void
__ubsan_handle_add_overflow_abort(RTABI_OverflowData *Data,
                                  RTABI_ValueHandle LHS,
                                  RTABI_ValueHandle RHS)
{
  handle_integer_overflow(Data, LHS, RHS, "+");
  ubsan_abort();
}
void
__ubsan_handle_sub_overflow(RTABI_OverflowData *Data, RTABI_ValueHandle LHS, RTABI_ValueHandle RHS)
{
  handle_integer_overflow(Data, LHS, RHS, "+");
}
void
__ubsan_handle_sub_overflow_abort(RTABI_OverflowData *Data,
                                  RTABI_ValueHandle LHS,
                                  RTABI_ValueHandle RHS)
{
  handle_integer_overflow(Data, LHS, RHS, "-");
  ubsan_abort();
}
void
__ubsan_handle_mul_overflow(RTABI_OverflowData *Data, RTABI_ValueHandle LHS, RTABI_ValueHandle RHS)
{
  handle_integer_overflow(Data, LHS, RHS, "+");
}
void
__ubsan_handle_mul_overflow_abort(RTABI_OverflowData *Data,
                                  RTABI_ValueHandle LHS,
                                  RTABI_ValueHandle RHS)
{
  handle_integer_overflow(Data, LHS, RHS, "*");
  ubsan_abort();
}




static void
handle_negate_overflow(RTABI_OverflowData *Data, RTABI_ValueHandle OldVal)
{
  todo("implement me!\n");
}

void
__ubsan_handle_negate_overflow(RTABI_OverflowData *Data, RTABI_ValueHandle OldVal)
{
  handle_negate_overflow(Data, OldVal);
}
void
__ubsan_handle_negate_overflow_abort(RTABI_OverflowData *Data, RTABI_ValueHandle OldVal)
{
  handle_negate_overflow(Data, OldVal);
  ubsan_abort();
}




static void
handle_divrem_overflow(RTABI_OverflowData *Data, RTABI_ValueHandle LHS, RTABI_ValueHandle RHS)
{
  todo("implement me!\n");
}

void
__ubsan_handle_divrem_overflow(RTABI_OverflowData *Data,
                               RTABI_ValueHandle LHS,
                               RTABI_ValueHandle RHS)
{
  handle_divrem_overflow(Data, LHS, RHS);
}
void
__ubsan_handle_divrem_overflow_abort(RTABI_OverflowData *Data,
                                     RTABI_ValueHandle LHS,
                                     RTABI_ValueHandle RHS)
{
  handle_divrem_overflow(Data, LHS, RHS);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// SHIFT OUT OF BOUNDS




static void
handle_shift_out_of_bounds(RTABI_ShiftOutOfBoundsData *Data,
                           RTABI_ValueHandle LHS,
                           RTABI_ValueHandle RHS)
{
  todo("implement me!\n");
}

void
__ubsan_handle_shift_out_of_bounds(RTABI_ShiftOutOfBoundsData *Data,
                                   RTABI_ValueHandle LHS,
                                   RTABI_ValueHandle RHS)
{
  handle_shift_out_of_bounds(Data, LHS, RHS);
}
void
__ubsan_handle_shift_out_of_bounds_abort(RTABI_ShiftOutOfBoundsData *Data,
                                         RTABI_ValueHandle LHS,
                                         RTABI_ValueHandle RHS)
{
  handle_shift_out_of_bounds(Data, LHS, RHS);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// ARRAY OUT OF BOUNDS




static void
handle_out_of_bounds(RTABI_OutOfBoundsData *Data, RTABI_ValueHandle Index)
{
  todo("implement me!\n");
}

void
__ubsan_handle_out_of_bounds(RTABI_OutOfBoundsData *Data, RTABI_ValueHandle Index)
{
  handle_out_of_bounds(Data, Index);
}
void
__ubsan_handle_out_of_bounds_abort(RTABI_OutOfBoundsData *Data, RTABI_ValueHandle Index)
{
  handle_out_of_bounds(Data, Index);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// LOCAL OUT OF BOUNDS




static void
handle_local_out_of_bounds()
{
  todo("implement me!\n");
}

void
__ubsan_handle_local_out_of_bounds()
{
  handle_local_out_of_bounds();
}
void
__ubsan_handle_local_out_of_bounds_abort()
{
  handle_local_out_of_bounds();
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// UNREACHABLES




void
__ubsan_handle_builtin_unreachable(RTABI_UnreachableData *Data)
{
  todo("implement me!\n");
}

void
__ubsan_handle_missing_return(RTABI_UnreachableData *Data)
{
  todo("implement me!\n");
}




// -------------------------------------------------------------------------------------------------
// VLA BOUNDS




static void
handle_vla_bound_not_positive(RTABI_VLABoundData *Data, RTABI_ValueHandle Bound)
{
  todo("implement me!\n");
}

void
__ubsan_handle_vla_bound_not_positive(RTABI_VLABoundData *Data, RTABI_ValueHandle Bound)
{
  handle_vla_bound_not_positive(Data, Bound);
}
void
__ubsan_handle_vla_bound_not_positive_abort(RTABI_VLABoundData *Data, RTABI_ValueHandle Bound)
{
  handle_vla_bound_not_positive(Data, Bound);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// FLOAT CAST OVERFLOW




static void
handle_float_cast_overflow(void *Data, RTABI_ValueHandle From)
{
  todo("implement me!\n");
}

void
__ubsan_handle_float_cast_overflow(void *Data, RTABI_ValueHandle From)
{
  handle_float_cast_overflow(Data, From);
}
void
__ubsan_handle_float_cast_overflow_abort(void *Data, RTABI_ValueHandle From)
{
  handle_float_cast_overflow(Data, From);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// LOAD INVALID VALUE




static void
handle_load_invalid_value(RTABI_InvalidValueData *Data, RTABI_ValueHandle Val)
{
  todo("implement me!\n");
}

void
__ubsan_handle_load_invalid_value(RTABI_InvalidValueData *Data, RTABI_ValueHandle Val)
{
  handle_load_invalid_value(Data, Val);
}
void
__ubsan_handle_load_invalid_value_abort(RTABI_InvalidValueData *Data, RTABI_ValueHandle Val)
{
  handle_load_invalid_value(Data, Val);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// IMPLICIT CONVERSION CHECKS




static void
handle_implicit_conversion(RTABI_ImplicitConversionData *Data,
                           RTABI_ValueHandle Src,
                           RTABI_ValueHandle Dst)
{
  todo("implement me!\n");
}

void
__ubsan_handle_implicit_conversion(RTABI_ImplicitConversionData *Data,
                                   RTABI_ValueHandle Src,
                                   RTABI_ValueHandle Dst)
{
  handle_implicit_conversion(Data, Src, Dst);
}
void
__ubsan_handle_implicit_conversion_abort(RTABI_ImplicitConversionData *Data,
                                         RTABI_ValueHandle Src,
                                         RTABI_ValueHandle Dst)
{
  handle_implicit_conversion(Data, Src, Dst);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// INVALID BUILTIN CALLS




static void
handle_invalid_builtin(RTABI_InvalidBuiltinData *Data)
{
  todo("implement me!\n");
}

void
__ubsan_handle_invalid_builtin(RTABI_InvalidBuiltinData *Data)
{
  handle_invalid_builtin(Data);
}
void
__ubsan_handle_invalid_builtin_abort(RTABI_InvalidBuiltinData *Data)
{
  handle_invalid_builtin(Data);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// NULL RETURNS




static void
handle_nonnull_return(RTABI_NonNullReturnData *Data, RTABI_SourceLocation *Loc)
{
  todo("implement me!\n");
}

void
__ubsan_handle_nonnull_return_v1(RTABI_NonNullReturnData *Data, RTABI_SourceLocation *Loc)
{
  handle_nonnull_return(Data, Loc);
}
void
__ubsan_handle_nonnull_return_v1_abort(RTABI_NonNullReturnData *Data, RTABI_SourceLocation *Loc)
{
  handle_nonnull_return(Data, Loc);
  ubsan_abort();
}




static void
handle_nullability_return(RTABI_NonNullReturnData *Data, RTABI_SourceLocation *Loc)
{
  todo("implement me!\n");
}

void
__ubsan_handle_nullability_return_v1(RTABI_NonNullReturnData *Data, RTABI_SourceLocation *Loc)
{
  handle_nullability_return(Data, Loc);
}
void
__ubsan_handle_nullability_return_v1_abort(RTABI_NonNullReturnData *Data, RTABI_SourceLocation *Loc)
{
  handle_nullability_return(Data, Loc);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// NULL ARGS




static void
handle_nonnull_arg(RTABI_NonNullArgData *Data)
{
  todo("implement me!\n");
}

void
__ubsan_handle_nonnull_arg(RTABI_NonNullArgData *Data)
{
  handle_nonnull_arg(Data);
}
void
__ubsan_handle_nonnull_arg_abort(RTABI_NonNullArgData *Data)
{
  handle_nonnull_arg(Data);
  ubsan_abort();
}




static void
handle_nullability_arg(RTABI_NonNullArgData *Data)
{
  todo("implement me!\n");
}

void
__ubsan_handle_nullability_arg(RTABI_NonNullArgData *Data)
{
  handle_nullability_arg(Data);
}
void
__ubsan_handle_nullability_arg_abort(RTABI_NonNullArgData *Data)
{
  handle_nullability_arg(Data);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// POINTER OVERFLOW




static void
handle_pointer_overflow(RTABI_PointerOverflowData *Data,
                        RTABI_ValueHandle Base,
                        RTABI_ValueHandle Result)
{
  // enum error_type et;

  // if(Base == 0 && Result == 0)
  //   et = ET_NullptrWithOffset;
  // else if (Base == 0 && Result != 0)
  //   et = ET_NullptrWithNonZeroOffset;
  // else if (Base != 0 && Result == 0)
  //   et = ET_NullptrAfterNonZeroOffset;
  // else
  //   et = ET_PointerOverflow;

  // ubsan_report_begin(&Data->loc, et);

  // if(et == ET_NullptrWithOffset) {
  //   printf("applying zero offset to null pointer\n");
  // }

  // ubsan_report_end();
  todo("implement me!\n");
}

void
__ubsan_handle_pointer_overflow(RTABI_PointerOverflowData *Data,
                                RTABI_ValueHandle Base,
                                RTABI_ValueHandle Result)
{
  handle_pointer_overflow(Data, Base, Result);
}
void
__ubsan_handle_pointer_overflow_abort(RTABI_PointerOverflowData *Data,
                                      RTABI_ValueHandle Base,
                                      RTABI_ValueHandle Result)
{
  handle_pointer_overflow(Data, Base, Result);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// FUNCTION TYPE MISMATCH




static void
handle_function_type_mismatch(RTABI_FunctionTypeMismatchData *Data, RTABI_ValueHandle Value)
{
  todo("implement me!\n");
}

void
__ubsan_handle_function_type_mismatch(RTABI_FunctionTypeMismatchData *Data, RTABI_ValueHandle Value)
{
  handle_function_type_mismatch(Data, Value);
}
void
__ubsan_handle_function_type_mismatch_abort(RTABI_FunctionTypeMismatchData *Data,
                                            RTABI_ValueHandle Value)
{
  handle_function_type_mismatch(Data, Value);
  ubsan_abort();
}
