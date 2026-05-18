#include "typed_value.h"

#include "debug.h"
#include "type_descriptors.h"

#include <stddef.h>
#include <stdio.h>

void
dump_typed_value(char const * label, TypedValue v)
{
    if (!debug_is_enabled(DEBUG_LEVEL_INFO))
    {
        return;
    }

    fprintf(stderr, "--------------------\n");
    fprintf(stderr, "TypedValue: %s\n", label);
    fprintf(stderr, "\tType descriptor: %p\n", (void *)v.type_info);

    if (v.value != NULL)
    {
        fprintf(stderr, "\tValue: %p\n\tis_lvalue: %d\n", (void *)v.value, v.is_lvalue);
        char * val_str = LLVMPrintValueToString(v.value);
        debug_info("\tType contents: %s", val_str);
        LLVMDisposeMessage(val_str);
    }
    if (v.bitfield.bit_width > 0)
    {
        fprintf(stderr, "bit width: %u\n", v.bitfield.bit_width);
        fprintf(stderr, "bit offset: %u\n", v.bitfield.bit_offset);
    }

    dump_type_descriptor(label, v.type_info, DEBUG_LEVEL_INFO);

    fprintf(stderr, "--------------------\n\n");
}

TypedValue
create_typed_value(LLVMValueRef val, TypeDescriptor const * desc, bool is_lvalue)
{
    return (TypedValue){
        .value = val,
        .type_info = desc,
        .is_lvalue = is_lvalue,
    };
}

bool
typed_value_switch_to_pointee(TypedValue * tv)
{
    if (tv == NULL || tv->type_info == NULL)
    {
        return false;
    }
    tv->type_info = tv->type_info->pointee;

    return true;
}
