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
    LLVMTypeRef type;         // The actual type (e.g., i32, struct.foo)
    LLVMTypeRef pointee_type; // If it's a pointer, what does it point to?
} TypedValue;

extern TypedValue NullTypedValue;

void dump_typed_value(char const * label, TypedValue v);
