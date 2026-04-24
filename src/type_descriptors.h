#pragma once

#include <llvm-c/Core.h>
#include <stddef.h>

typedef enum
{
    NCC_TYPE_KIND_BUILTIN,
    NCC_TYPE_KIND_POINTER,
    NCC_TYPE_KIND_ARRAY,
    NCC_TYPE_KIND_STRUCT,
    NCC_TYPE_KIND_UNION,
    NCC_TYPE_KIND_TYPEDEF,
    NCC_TYPE_KIND_FUNCTION,
} type_descriptor_type_kind_t;

typedef struct TypeDescriptors TypeDescriptors;
typedef struct TypeDescriptor TypeDescriptor;
typedef struct TypeDescriptor
{
    type_descriptor_type_kind_t kind;
    LLVMTypeRef llvm_type;

    // Relationships
    TypeDescriptor const * pointee; // For pointers/arrays

    /* TODO: Specifiers and qualifiers. */
} TypeDescriptor;

TypeDescriptors * type_descriptors_create_list(void);

void type_descriptors_destroy_list(TypeDescriptors * list);

TypeDescriptor const * type_descriptors_get_or_create(
    TypeDescriptors * list, type_descriptor_type_kind_t kind, LLVMTypeRef type, TypeDescriptor const * pointee
);
