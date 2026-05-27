#include "typed_value.h"

#include "debug.h"
#include "type_descriptors.h"

#include <stddef.h>
#include <stdio.h>

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
