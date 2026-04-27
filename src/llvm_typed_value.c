#include "llvm_typed_value.h"

#include "debug.h"
#include "type_descriptors.h"

#include <stddef.h>
#include <stdio.h>

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
    fprintf(stderr, "\ttype: %p (%d)\n", (void *)v.type, (v.type != NULL) ? (int)LLVMGetTypeKind(v.type) : -1);
    fprintf(
        stderr,
        "\tpointee type: %p (%d)\n",
        (void *)v.pointee_type,
        (v.pointee_type != NULL) ? (int)LLVMGetTypeKind(v.pointee_type) : -1
    );
    fprintf(stderr, "\tbit width: %u\n", v.bit_width);
    fprintf(stderr, "\tbit offset: %u\n", v.bit_offset);
    fprintf(stderr, "--------------------\n\n");
}

TypedValue
create_typed_value_llvm(LLVMValueRef val, LLVMTypeRef val_type, LLVMTypeRef pointee_type, bool is_lvalue)
{
    // This function should be used when we only have LLVM type info available, and not the full TypeDescriptor
    // metadata. It should eventually not be needed at all.
    return (TypedValue){
        .value = val,
        .is_lvalue = is_lvalue,
        .type = val_type,
        .pointee_type = pointee_type,
    };
}

TypedValue
create_typed_value(LLVMValueRef val, TypeDescriptor const * desc, bool is_lvalue)
{
    return (TypedValue){
        .value = val,
        .type_info = desc,
        .is_lvalue = is_lvalue,
        // Carry over metadata from the descriptor
        .type = desc != NULL ? desc->llvm_type : NULL,
        .pointee_type = (desc != NULL && desc->pointee != NULL) ? desc->pointee->llvm_type : NULL
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
    if (tv == NULL)
    {
        return NULL;
    }
    if (tv->type_info != NULL)
    {
        return tv->type_info->llvm_type;
    }
    return tv->type;
}

LLVMTypeRef
typed_value_get_pointee_llvm(TypedValue const * tv)
{
    if (tv == NULL)
    {
        return NULL;
    }
    if (tv->type_info != NULL)
    {
        if (tv->type_info->pointee != NULL)
        {
            return tv->type_info->pointee->llvm_type;
        }
        else
        {
            return NULL;
        }
    }

    return tv->pointee_type;
}

bool
typed_value_switch_to_pointee(TypedValue * tv)
{
    if (tv == NULL)
    {
        return false;
    }
    if (tv->type_info != NULL)
    {
        tv->type_info = tv->type_info->pointee;
        return true;
    }
    /* Should eventually be removed once we switch to using TypeDescriptors exclusively. */
    if (tv->pointee_type != NULL)
    {
        tv->type = tv->pointee_type;
        tv->pointee_type = NULL; // Clear pointee_type since we've switched to it
        // We don't have full TypeDescriptor info, so we can't update type_info or pointee_type accurately.
        return true;
    }
    return false;
}

void
typed_value_assign_type_desc(TypedValue * tv, TypeDescriptor const * desc)
{
    if (tv == NULL)
    {
        return;
    }
    tv->type_info = desc;

    /* Legacy support. */
    tv->type = desc->llvm_type;
    tv->pointee_type = (desc->pointee != NULL) ? desc->pointee->llvm_type : NULL;
}