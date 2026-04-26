#pragma once

#include "type_descriptors.h"

#include <llvm-c/Core.h>
#include <stdbool.h>

typedef struct
{
    bool is_lvalue;
    bool is_unsigned;
    unsigned bit_width;
    unsigned bit_offset;
    LLVMValueRef value;

    /* type-related data. */
    TypeDescriptor const * type_info; // The high-level type descriptor for this value

    /* These will eventually be removed as all info will be found in the type_info. */
    LLVMTypeRef type;         // The actual type (e.g., i32, struct.foo)
    LLVMTypeRef pointee_type; // If it's a pointer, what does it point to?
} TypedValue;

extern TypedValue NullTypedValue;

void dump_typed_value(char const * label, TypedValue v);

TypedValue create_typed_value_llvm(LLVMValueRef val, LLVMTypeRef val_type, LLVMTypeRef pointee_type, bool is_lvalue);

TypedValue create_typed_value(LLVMValueRef val, TypeDescriptor const * desc, bool is_lvalue);

TypeDescriptor const * typed_value_get_descriptor(TypedValue const * tv);

LLVMTypeRef typed_value_get_llvm_type(TypedValue const * tv);

LLVMTypeRef typed_value_get_pointee_llvm(TypedValue const * tv);
