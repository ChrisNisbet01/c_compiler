#pragma once

#include "type_specifier.h"

#include <llvm-c/Core.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct
{
    bool is_const;
    bool is_volatile;
} TypeQualifier;

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

typedef struct
{
    TypeDescriptor const * return_type;
    unsigned param_count;
    TypeDescriptor const ** params; // Allocated once in the registry
    bool is_variadic;
} FunctionMetadata;

typedef struct TypeDescriptor
{
    type_descriptor_type_kind_t kind;

    LLVMTypeRef llvm_type; /* The underlying LLVM type. */

    TypeQualifier qualifiers;
    TypeSpecifier specifiers;

    // Relationships
    TypeDescriptor const * pointee; // For pointers/arrays

    /* Function-specific */
    FunctionMetadata function_metadata;
} TypeDescriptor;

TypeDescriptors * type_descriptors_create_registry(LLVMContextRef context);

void type_descriptors_destroy_registry(TypeDescriptors * registry);

TypeDescriptor const *
get_or_create_pointer_type(TypeDescriptors * registry, TypeDescriptor const * pointee, TypeQualifier qualifiers);

TypeDescriptor const * get_or_create_builtin_type(TypeDescriptors * registry, TypeSpecifier specs, TypeQualifier quals);

TypeDescriptor const *
register_struct_type(TypeDescriptors * registry, LLVMTypeRef llvm_struct, TypeQualifier const quals, bool is_union);

TypeDescriptor const * get_or_create_function_type(
    TypeDescriptors * registry,
    TypeDescriptor const * ret_type,
    TypeDescriptor const ** params,
    size_t param_count,
    bool is_variadic
);

TypeDescriptor const *
get_type_descriptor_from_specifiers(TypeDescriptors * registry, TypeSpecifier const specs, TypeQualifier const quals);
