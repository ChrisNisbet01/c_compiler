#pragma once

#include "struct_bitfield_data.h"

#include <llvm-c/Core.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct TypeDescriptor_st TypeDescriptor;

typedef struct struct_field
{
    char * name;
    TypeDescriptor const * type_desc;
    LLVMTypeRef type;
    LLVMTypeRef pointee_struct_type; /* If type is a pointer to a struct, the struct type it points to */
    struct_bitfield_data_t bitfield;
} struct_field_t;

typedef struct
{
    size_t num_members;
    struct_field_t * members;
} struct_or_union_members_st;
