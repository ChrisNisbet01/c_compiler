#include "llvm_typed_value.h"

#include "debug.h"
#include "type_descriptors.h"

#include <stddef.h>
#include <stdio.h>

void
dump_type_descriptor(char const * name, TypeDescriptor const * desc, debug_level_t level)
{
    if (desc == NULL)
    {
        return;
    }

    if (level < debug_get_level())
    {
        return;
    }
    fprintf(
        stderr,
        "TypeDescriptor: %s, kind=%d llvm_type_kind=%d\n",
        name,
        desc->kind,
        desc->llvm_type != NULL ? (int)LLVMGetTypeKind(desc->llvm_type) : -1
    );
}

void
dump_typed_value(char const * label, TypedValue v)
{
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
    fprintf(stderr, "bit width: %u\n", v.bitfield.bit_width);
    fprintf(stderr, "bit offset: %u\n", v.bitfield.bit_offset);
    fprintf(stderr, "storage index: %u\n", v.bitfield.storage_index);

    dump_type_descriptor("val", v.type_info, DEBUG_LEVEL_INFO);

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

TypeDescriptor const *
typed_value_get_descriptor(TypedValue const * tv)
{
    if (tv == NULL)
    {
        return NULL;
    }
    return tv->type_info;
}

LLVMTypeRef
typed_value_get_llvm_type(TypedValue const * tv)
{
    if (tv == NULL || tv->type_info != NULL)
    {
        return NULL;
    }

    return tv->type_info->llvm_type;
}

LLVMTypeRef
typed_value_get_pointee_llvm(TypedValue const * tv)
{
    if (tv == NULL || tv->type_info == NULL)
    {
        return NULL;
    }

    return tv->type_info->pointee != NULL ? tv->type_info->pointee->llvm_type : NULL;
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
