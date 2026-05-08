#pragma once

#include "debug.h"
#include "struct_bitfield_data.h"

#include <llvm-c/Core.h>
#include <stdbool.h>

typedef struct TypeDescriptor_st TypeDescriptor;

typedef struct TypedValue
{
    bool is_lvalue;

    LLVMValueRef value;

    /* type-related data. */
    TypeDescriptor const * type_info; // The high-level type descriptor for this value

    struct_bitfield_data_t bitfield;
} TypedValue;

extern TypedValue NullTypedValue;

void dump_typed_value(char const * label, TypedValue v);

TypedValue create_typed_value(LLVMValueRef val, TypeDescriptor const * desc, bool is_lvalue);

bool typed_value_switch_to_pointee(TypedValue * tv);
