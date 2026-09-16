#include <generic/ubsan.h>
#include <generic/printf.h>
#include <generic/backtrace.h>

#define TODO "\x1b[33mTODO\x1b[0m"

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




typedef enum
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
} ErrorType;
static const char *ERROR_TYPE_SUMMARIES[] = {
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




typedef struct
{
  backtrace_cursor trace_cursor;
  bool unrecoverable;
} ReportOpts;
// Note: it's okay to create a backtrace cursor if backtrace isn't enabled, but it IS forbidden to
//       use said cursor if backtrace functionality is not enabled.
#define UBSAN_OPTS(...) \
  (ReportOpts){ .trace_cursor = backtrace_cursor_new() __VA_OPT__(, ) __VA_ARGS__ };




static uint32_t NEXT_REPORT_INDEX = 0;
static void
ubsan_report_begin(SourceLocation *loc, ErrorType et, ReportOpts opts)
{
  printf("\n============================== [UBSAN REPORT %d] ==============================\n",
         NEXT_REPORT_INDEX);
  printf("%s:%u:%u: %s: ", loc->filename, loc->line, loc->col, ERROR_TYPE_SUMMARIES[et]);
}
static void
ubsan_report_end(ReportOpts opts)
{
  uintptr_t pc;
  const char *sym;

  if (backtrace_enabled()) {
    printf("Backtrace:\n");
    do {
      backtrace_cursor_read(&opts.trace_cursor, &pc, &sym);
      printf("\tat %#10x in \x1b[1m%s\x1b[0m\n", pc, sym ? sym : "(unknown)");
    } while (backtrace_cursor_next(&opts.trace_cursor));
  }

  printf("============================== [ END REPORT %d ] ==============================\n",
         NEXT_REPORT_INDEX);
  NEXT_REPORT_INDEX++;
}
uint32_t
ubsan_report_count(void)
{
  return NEXT_REPORT_INDEX;
}




static void
print_value_int(const TypeDescriptor *td, ValueHandle val)
{
  uint32_t bit_width, byte_width, i;
  ValueHandle masked_val;
  uint8_t *intp;
  bool hit_nz;

  assert(td_is_int(td));

  bit_width = td_get_bit_width(td);
  byte_width = (bit_width + 7) / 8;

  if (!byte_width)
    printf("0x0");

  if (byte_width <= sizeof val) {
    masked_val = (~((~(ValueHandle)(0)) << bit_width)) & val;
    if (!masked_val)
      printf("0x0");
    else
      printf("%#zx", masked_val);
  } else {
    printf("0x");
    i = byte_width - 1;
    intp = (uint8_t *)val;
    masked_val = (~(0xff << (bit_width & 7))) & intp[i];
    hit_nz = false;
    while (1) {
      if (hit_nz)
        printf("%02hhx", (uint8_t)masked_val);
      else if (masked_val || !i)
        printf("%hhx", (uint8_t)masked_val);
      hit_nz |= !!masked_val;
      if (!i--)
        break;
      masked_val = intp[i];
    };
  }
}
static void
print_value(const TypeDescriptor *td, ValueHandle val)
{
  if (td_is_int(td)) {
    print_value_int(td, val);
  } else if (td->type_kind == TK_Float) {
    printf("TK_Float");
  } else {
    printf("TK_Unknown");
  }
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

static void
handle_type_mismatch(TypeMismatchData *Data, ValueHandle Pointer, ReportOpts opts)
{
  uintptr_t alignment;
  ErrorType ET;

  alignment = (uintptr_t)1 << Data->log_alignment;
  if (!Pointer) {
    ET = (Data->type_check_kind == TCK_NonnullAssign) ? ET_NullPointerUseWithNullability
                                                      : ET_NullPointerUse;
  } else if (Pointer & (alignment - 1)) {
    ET = ET_MisalignedPointerUse;
  } else {
    ET = ET_InsufficientObjectSize;
  }

  ubsan_report_begin(&Data->loc, ET, opts);

  switch (ET) {
    case ET_NullPointerUse:
    case ET_NullPointerUseWithNullability:
      printf("%s null pointer of type %s\n",
             TYPE_CHECK_KIND_NAMES[Data->type_check_kind],
             Data->ty->type_name);
      break;
    case ET_MisalignedPointerUse:
      printf("%s misaligned address %p for type %s which requires %d byte alignment\n",
             TYPE_CHECK_KIND_NAMES[Data->type_check_kind],
             (void *)Pointer,
             Data->ty->type_name,
             alignment);
      break;
    case ET_InsufficientObjectSize:
      printf("%s address %p with insufficient space for an object of type %s\n",
             TYPE_CHECK_KIND_NAMES[Data->type_check_kind],
             (void *)Pointer,
             Data->ty->type_name);
      break;
    default:
      panic("unreachable!\n");
  }

  if (Pointer)
    printf("NOTE: %p: pointer points to here\n", (void *)Pointer);

  ubsan_report_end(opts);
}

void
__ubsan_handle_type_mismatch_v1(TypeMismatchData *Data, ValueHandle Pointer)
{
  ReportOpts opts = UBSAN_OPTS(false);
  handle_type_mismatch(Data, Pointer, opts);
}
void
__ubsan_handle_type_mismatch_v1_abort(TypeMismatchData *Data, ValueHandle Pointer)
{
  ReportOpts opts = UBSAN_OPTS(true);
  handle_type_mismatch(Data, Pointer, opts);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// ALIGNMENT ASSUMPTION




static void
handle_alignment_assumption(AlignmentAssumptionData *Data,
                            ValueHandle Pointer,
                            ValueHandle Alignment,
                            ValueHandle Offset,
                            ReportOpts opts)
{
  todo("implement me\n");
}

void
__ubsan_handle_alignment_assumption(AlignmentAssumptionData *Data,
                                    ValueHandle Pointer,
                                    ValueHandle Alignment,
                                    ValueHandle Offset)
{
  ReportOpts opts = UBSAN_OPTS(false);
  handle_alignment_assumption(Data, Pointer, Alignment, Offset, opts);
}
void
__ubsan_handle_alignment_assumption_abort(AlignmentAssumptionData *Data,
                                          ValueHandle Pointer,
                                          ValueHandle Alignment,
                                          ValueHandle Offset)
{
  ReportOpts opts = UBSAN_OPTS(true);
  handle_alignment_assumption(Data, Pointer, Alignment, Offset, opts);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// INTEGER OVERFLOW




static void
handle_integer_overflow(OverflowData *Data,
                        ValueHandle LHS,
                        ValueHandle RHS,
                        const char *symbol,
                        ReportOpts opts)
{
  ErrorType ET;
  bool is_signed;

  is_signed = td_is_sint(Data->ty);
  ET = is_signed ? ET_SignedIntegerOverflow : ET_UnsignedIntegerOverflow;
  ubsan_report_begin(&Data->loc, ET, opts);

  printf("%s integer overflow: ", is_signed ? "signed" : "unsigned");
  print_value(Data->ty, LHS);
  printf(" %s ", symbol);
  print_value(Data->ty, RHS);
  printf(" cannot be represented in the type %s\n", Data->ty->type_name);

  ubsan_report_end(opts);
}

void
__ubsan_handle_add_overflow(OverflowData *Data, ValueHandle LHS, ValueHandle RHS)
{
  ReportOpts opts = UBSAN_OPTS(false);
  handle_integer_overflow(Data, LHS, RHS, "+", opts);
}
void
__ubsan_handle_add_overflow_abort(OverflowData *Data, ValueHandle LHS, ValueHandle RHS)
{
  ReportOpts opts = UBSAN_OPTS(true);
  handle_integer_overflow(Data, LHS, RHS, "+", opts);
  ubsan_abort();
}
void
__ubsan_handle_sub_overflow(OverflowData *Data, ValueHandle LHS, ValueHandle RHS)
{
  ReportOpts opts = UBSAN_OPTS(false);
  handle_integer_overflow(Data, LHS, RHS, "-", opts);
}
void
__ubsan_handle_sub_overflow_abort(OverflowData *Data, ValueHandle LHS, ValueHandle RHS)
{
  ReportOpts opts = UBSAN_OPTS(true);
  handle_integer_overflow(Data, LHS, RHS, "-", opts);
  ubsan_abort();
}
void
__ubsan_handle_mul_overflow(OverflowData *Data, ValueHandle LHS, ValueHandle RHS)
{
  ReportOpts opts = UBSAN_OPTS(false);
  handle_integer_overflow(Data, LHS, RHS, "*", opts);
}
void
__ubsan_handle_mul_overflow_abort(OverflowData *Data, ValueHandle LHS, ValueHandle RHS)
{
  ReportOpts opts = UBSAN_OPTS(true);
  handle_integer_overflow(Data, LHS, RHS, "*", opts);
  ubsan_abort();
}




static void
handle_negate_overflow(OverflowData *Data, ValueHandle OldVal, ReportOpts opts)
{
  ErrorType ET;
  bool is_signed;

  is_signed = td_is_sint(Data->ty);
  ET = is_signed ? ET_SignedIntegerOverflow : ET_UnsignedIntegerOverflow;

  ubsan_report_begin(&Data->loc, ET, opts);

  printf("%s integer overflow: negation of ", is_signed ? "signed" : "unsigned");
  print_value(Data->ty, OldVal);
  printf(" cannot be represented in type %s\n", Data->ty->type_name);

  ubsan_report_end(opts);
}

void
__ubsan_handle_negate_overflow(OverflowData *Data, ValueHandle OldVal)
{
  ReportOpts opts = UBSAN_OPTS(false);
  handle_negate_overflow(Data, OldVal, opts);
}
void
__ubsan_handle_negate_overflow_abort(OverflowData *Data, ValueHandle OldVal)
{
  ReportOpts opts = UBSAN_OPTS(true);
  handle_negate_overflow(Data, OldVal, opts);
  ubsan_abort();
}




static bool
is_minus_one(const TypeDescriptor *td, ValueHandle val)
{
  uint32_t bit_width, i;
  ValueHandle imask;
  uint8_t *intp, v;

  if (!td_is_int(td))
    return false;
  if (td_is_uint(td))
    return false;

  bit_width = td_get_bit_width(td);
  i = (bit_width + 7) / 8;
  if (!i)
    return false;

  if (i <= 4) {
    imask = (~(ValueHandle)0) << bit_width;
    return !~(imask | (val & ~imask));
  } else {
    intp = (uint8_t *)val;
    v = (intp[--i] & ~(0xff << (bit_width & 7))) | (0xff << (bit_width & 7));
    while (1) {
      if (~v)
        return false;
      if (!i)
        return true;
      v = intp[--i];
    }
  }
}

static void
handle_divrem_overflow(OverflowData *Data, ValueHandle LHS, ValueHandle RHS, ReportOpts opts)
{
  ErrorType ET;

  // cases:
  //  1. rhs=-1   => ET_SignedIntegerOverflow
  //  2. ty:int   => ET_IntegerDivideByZero
  //  3. ty:float => ET_FloatDivideByZero

  if (is_minus_one(Data->ty, RHS))
    ET = ET_SignedIntegerOverflow;
  else if (td_is_int(Data->ty))
    ET = ET_IntegerDivideByZero;
  else if (td_is_float(Data->ty))
    ET = ET_FloatDivideByZero;
  else
    panic("handle_divrem_overflow: RHS is not an integer or float");

  ubsan_report_begin(&Data->loc, ET, opts);

  if (ET == ET_SignedIntegerOverflow) {
    printf("division of ");
    print_value(Data->ty, LHS);
    printf(" by -1 cannot be represented in type %s\n", Data->ty->type_name);
  } else {
    printf("division by zero\n");
  }

  ubsan_report_end(opts);
}

void
__ubsan_handle_divrem_overflow(OverflowData *Data, ValueHandle LHS, ValueHandle RHS)
{
  ReportOpts opts = UBSAN_OPTS(false);
  handle_divrem_overflow(Data, LHS, RHS, opts);
}
void
__ubsan_handle_divrem_overflow_abort(OverflowData *Data, ValueHandle LHS, ValueHandle RHS)
{
  ReportOpts opts = UBSAN_OPTS(true);
  handle_divrem_overflow(Data, LHS, RHS, opts);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// SHIFT OUT OF BOUNDS




static void
handle_shift_out_of_bounds(ShiftOutOfBoundsData *Data, ValueHandle LHS, ValueHandle RHS)
{
  todo("implement me!\n");
}

void
__ubsan_handle_shift_out_of_bounds(ShiftOutOfBoundsData *Data, ValueHandle LHS, ValueHandle RHS)
{
  handle_shift_out_of_bounds(Data, LHS, RHS);
}
void
__ubsan_handle_shift_out_of_bounds_abort(ShiftOutOfBoundsData *Data,
                                         ValueHandle LHS,
                                         ValueHandle RHS)
{
  handle_shift_out_of_bounds(Data, LHS, RHS);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// ARRAY OUT OF BOUNDS




static void
handle_out_of_bounds(OutOfBoundsData *Data, ValueHandle Index)
{
  todo("implement me!\n");
}

void
__ubsan_handle_out_of_bounds(OutOfBoundsData *Data, ValueHandle Index)
{
  handle_out_of_bounds(Data, Index);
}
void
__ubsan_handle_out_of_bounds_abort(OutOfBoundsData *Data, ValueHandle Index)
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
__ubsan_handle_builtin_unreachable(UnreachableData *Data)
{
  todo("implement me!\n");
}

void
__ubsan_handle_missing_return(UnreachableData *Data)
{
  todo("implement me!\n");
}




// -------------------------------------------------------------------------------------------------
// VLA BOUNDS




static void
handle_vla_bound_not_positive(VLABoundData *Data, ValueHandle Bound)
{
  todo("implement me!\n");
}

void
__ubsan_handle_vla_bound_not_positive(VLABoundData *Data, ValueHandle Bound)
{
  handle_vla_bound_not_positive(Data, Bound);
}
void
__ubsan_handle_vla_bound_not_positive_abort(VLABoundData *Data, ValueHandle Bound)
{
  handle_vla_bound_not_positive(Data, Bound);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// FLOAT CAST OVERFLOW




static void
handle_float_cast_overflow(void *Data, ValueHandle From)
{
  todo("implement me!\n");
}

void
__ubsan_handle_float_cast_overflow(void *Data, ValueHandle From)
{
  handle_float_cast_overflow(Data, From);
}
void
__ubsan_handle_float_cast_overflow_abort(void *Data, ValueHandle From)
{
  handle_float_cast_overflow(Data, From);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// LOAD INVALID VALUE




static void
handle_load_invalid_value(InvalidValueData *Data, ValueHandle Val)
{
  todo("implement me!\n");
}

void
__ubsan_handle_load_invalid_value(InvalidValueData *Data, ValueHandle Val)
{
  handle_load_invalid_value(Data, Val);
}
void
__ubsan_handle_load_invalid_value_abort(InvalidValueData *Data, ValueHandle Val)
{
  handle_load_invalid_value(Data, Val);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// IMPLICIT CONVERSION CHECKS




static void
handle_implicit_conversion(ImplicitConversionData *Data,
                           ValueHandle Src,
                           ValueHandle Dst,
                           ReportOpts opts)
{
  ErrorType ET;

  switch (Data->kind) {
    case ICCK_IntegerTruncation:
      panic("handle_implicit_conversion: ICCK_IntegerTruncation is no longer supported. Please "
            "update your compiler.\n");
    case ICCK_UnsignedIntegerTruncation:
      ET = ET_ImplicitUnsignedIntegerTruncation;
      break;
    case ICCK_SignedIntegerTruncation:
      ET = ET_ImplicitSignedIntegerTruncation;
      break;
    case ICCK_IntegerSignChange:
      ET = ET_ImplicitSignedIntegerSignChange;
      break;
    case ICCK_SignedIntegerTruncationOrSignChange:
      ET = ET_ImplicitSignedIntegerTruncationOrSignChange;
      break;
  }

  ubsan_report_begin(&Data->loc, ET, opts);

  // Destination is a bitfield of size bitfield_bits (we don't care about the reverse case).
  printf("implicit conversion from type %s of value ", Data->from_ty->type_name);
  print_value(Data->from_ty, Src);
  printf(" (%d-bit, %s) to type %s changed the value to ",
         td_get_bit_width(Data->from_ty),
         td_is_sint(Data->from_ty) ? "signed" : "unsigned",
         Data->to_ty->type_name);
  print_value(Data->to_ty, Dst);
  printf(" (%d-bit%s, %s)\n",
         Data->bitfield_bits ?: td_get_bit_width(Data->to_ty),
         Data->bitfield_bits ? " bitfield " : "",
         td_is_sint(Data->to_ty) ? "signed" : "unsigned");

  ubsan_report_end(opts);
}

void
__ubsan_handle_implicit_conversion(ImplicitConversionData *Data, ValueHandle Src, ValueHandle Dst)
{
  ReportOpts opts = UBSAN_OPTS(false);
  handle_implicit_conversion(Data, Src, Dst, opts);
}
void
__ubsan_handle_implicit_conversion_abort(ImplicitConversionData *Data,
                                         ValueHandle Src,
                                         ValueHandle Dst)
{
  ReportOpts opts = UBSAN_OPTS(true);
  handle_implicit_conversion(Data, Src, Dst, opts);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// INVALID BUILTIN CALLS




static void
handle_invalid_builtin(InvalidBuiltinData *Data)
{
  todo("implement me!\n");
}

void
__ubsan_handle_invalid_builtin(InvalidBuiltinData *Data)
{
  handle_invalid_builtin(Data);
}
void
__ubsan_handle_invalid_builtin_abort(InvalidBuiltinData *Data)
{
  handle_invalid_builtin(Data);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// NULL RETURNS




static void
handle_nonnull_return(NonNullReturnData *Data, SourceLocation *Loc)
{
  todo("implement me!\n");
}

void
__ubsan_handle_nonnull_return_v1(NonNullReturnData *Data, SourceLocation *Loc)
{
  handle_nonnull_return(Data, Loc);
}
void
__ubsan_handle_nonnull_return_v1_abort(NonNullReturnData *Data, SourceLocation *Loc)
{
  handle_nonnull_return(Data, Loc);
  ubsan_abort();
}




static void
handle_nullability_return(NonNullReturnData *Data, SourceLocation *Loc)
{
  todo("implement me!\n");
}

void
__ubsan_handle_nullability_return_v1(NonNullReturnData *Data, SourceLocation *Loc)
{
  handle_nullability_return(Data, Loc);
}
void
__ubsan_handle_nullability_return_v1_abort(NonNullReturnData *Data, SourceLocation *Loc)
{
  handle_nullability_return(Data, Loc);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// NULL ARGS




static void
handle_nonnull_arg(NonNullArgData *Data)
{
  todo("implement me!\n");
}

void
__ubsan_handle_nonnull_arg(NonNullArgData *Data)
{
  handle_nonnull_arg(Data);
}
void
__ubsan_handle_nonnull_arg_abort(NonNullArgData *Data)
{
  handle_nonnull_arg(Data);
  ubsan_abort();
}




static void
handle_nullability_arg(NonNullArgData *Data)
{
  todo("implement me!\n");
}

void
__ubsan_handle_nullability_arg(NonNullArgData *Data)
{
  handle_nullability_arg(Data);
}
void
__ubsan_handle_nullability_arg_abort(NonNullArgData *Data)
{
  handle_nullability_arg(Data);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// POINTER OVERFLOW




static void
handle_pointer_overflow(PointerOverflowData *Data, ValueHandle Base, ValueHandle Result)
{
  // ErrorType et;

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
__ubsan_handle_pointer_overflow(PointerOverflowData *Data, ValueHandle Base, ValueHandle Result)
{
  handle_pointer_overflow(Data, Base, Result);
}
void
__ubsan_handle_pointer_overflow_abort(PointerOverflowData *Data,
                                      ValueHandle Base,
                                      ValueHandle Result)
{
  handle_pointer_overflow(Data, Base, Result);
  ubsan_abort();
}




// -------------------------------------------------------------------------------------------------
// FUNCTION TYPE MISMATCH




static void
handle_function_type_mismatch(FunctionTypeMismatchData *Data, ValueHandle Value)
{
  todo("implement me!\n");
}

void
__ubsan_handle_function_type_mismatch(FunctionTypeMismatchData *Data, ValueHandle Value)
{
  handle_function_type_mismatch(Data, Value);
}
void
__ubsan_handle_function_type_mismatch_abort(FunctionTypeMismatchData *Data, ValueHandle Value)
{
  handle_function_type_mismatch(Data, Value);
  ubsan_abort();
}
