#pragma once

#include <llvm-c/Core.h>
#include <stdbool.h>

typedef struct
{
    bool is_unsigned;
    int long_count; // 0 = int, 1 = long, 2 = long long
    bool is_void;
    bool is_bool;
    bool is_short;
    bool is_char;
    bool is_int;
    bool is_float;
    bool is_double;
    // ... etc
} TypeSpecifier;

typedef struct
{
    bool is_lvalue;
    bool is_unsigned;
    unsigned bit_width;
    unsigned bit_offset;
    LLVMValueRef value;
    LLVMTypeRef type;         // The actual type (e.g., i32, struct.foo)
    LLVMTypeRef pointee_type; // If it's a pointer, what does it point to?
    TypeSpecifier specifiers; // TODO - unsupported right now.
} TypedValue;

extern TypedValue NullTypedValue;

void dump_typed_value(char const * label, TypedValue v);
