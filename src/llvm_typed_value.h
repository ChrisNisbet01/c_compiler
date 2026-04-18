#pragma once

#include <llvm-c/Core.h>

typedef struct
{
    LLVMValueRef value;
    LLVMTypeRef type;         // The actual type (e.g., i32, struct.foo)
    LLVMTypeRef pointee_type; // If it's a pointer, what does it point to?
} TypedValue;

extern TypedValue NullTypedValue;
