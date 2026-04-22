#include "llvm_typed_value.h"

#include <debug.h>
#include <stddef.h>
#include <stdio.h>

void
dump_typed_value(char const * label, TypedValue v)
{
    fprintf(stderr, "--------------------\n");
    fprintf(stderr, "TypedValue: %s\n", label);
    if (v.value != NULL)
    {
        fprintf(stderr, "has value: %p, is_lvalue: %d\n", (void *)v.value, v.is_lvalue);
        char * val_str = LLVMPrintValueToString(v.value);
        debug_info("Type contents: %s", val_str);
        LLVMDisposeMessage(val_str);
    }
    fprintf(stderr, "type: %p (%d)\n", (void *)v.type, (v.type != NULL) ? (int)LLVMGetTypeKind(v.type) : -1);
    fprintf(
        stderr,
        "pointee type: %p (%d)\n",
        (void *)v.pointee_type,
        (v.pointee_type != NULL) ? (int)LLVMGetTypeKind(v.pointee_type) : -1
    );
    fprintf(stderr, "bit width: %u\n", v.bit_width);
    fprintf(stderr, "bit offset: %u\n", v.bit_offset);
    fprintf(stderr, "--------------------\n\n");
}
