#pragma once

#include <llvm-c/Core.h>

typedef struct TypeDescriptor TypeDescriptor;

// --- Struct type info for member access ---
typedef struct
{
    unsigned bit_offset;
    unsigned bit_width;     // bit_width == 0 indicates this is not a bitfield or an unnamed bitfield
    unsigned storage_index; // -1 for regular fields, >=0 for bitfields = index of storage field
    uint64_t offset;        // offset from the start of the struct, allowing for padding.
} struct_bitfield_data_t;

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
