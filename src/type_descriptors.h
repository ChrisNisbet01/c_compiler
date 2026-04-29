#pragma once

#include "type_qualifiers.h"
#include "type_specifier.h"
#include "type_utils.h"

#include <llvm-c/Core.h>
#include <stdbool.h>
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

typedef struct
{
    TypeDescriptor const * return_type;
    unsigned param_count;
    TypeDescriptor const ** params; // Allocated once in the registry
    char const ** names;            // Allocated once in the registry
    bool is_variadic;
} FunctionMetadata;

typedef struct
{
    bool is_complete;
    struct_or_union_members_st members;
    /* TODO: add members. */
} StructMetaData;

typedef struct TypeDescriptor
{
    type_descriptor_type_kind_t kind;

    LLVMTypeRef llvm_type; /* The underlying LLVM type. */

    TypeQualifier qualifiers;
    TypeSpecifier specifiers;

    // Relationships
    TypeDescriptor const * pointee; // For pointers/arrays
    size_t array_size;              // 0 for unsized (int[]), >0 for sized (int[10])

    /* Function-specific */
    FunctionMetadata function_metadata;

    /* Struct/union-specific */
    StructMetaData struct_metadata;
} TypeDescriptor;

TypeDescriptors * type_descriptors_create_registry(LLVMContextRef context);

void type_descriptors_destroy_registry(TypeDescriptors * registry);

TypeDescriptor const *
get_or_create_array_type(TypeDescriptors * registry, TypeDescriptor const * element_type, size_t size);

TypeDescriptor const *
get_or_create_pointer_type(TypeDescriptors * registry, TypeDescriptor const * pointee, TypeQualifier qualifiers);

TypeDescriptor const * get_or_create_builtin_type(TypeDescriptors * registry, TypeSpecifier specs, TypeQualifier quals);

TypeDescriptor const * register_struct_type(
    TypeDescriptors * registry,
    LLVMTypeRef llvm_struct,
    TypeQualifier const quals,
    bool is_union,
    bool is_complete,
    struct_or_union_members_st * members
);

TypeDescriptor const * get_or_create_function_type(
    TypeDescriptors * registry,
    TypeDescriptor const * ret_type,
    TypeDescriptor const ** params,
    char const ** param_names,
    size_t param_count,
    bool is_variadic
);

TypeDescriptor const *
get_type_descriptor_from_specifiers(TypeDescriptors * registry, TypeSpecifier const specs, TypeQualifier const quals);

TypeDescriptor const * type_descriptor_get_pointee(TypeDescriptor const * desc);

TypeDescriptor const * find_descriptor_by_llvm_type(TypeDescriptors * registry, LLVMTypeRef type);

TypeDescriptor const *
create_fallback_descriptor_impl(TypeDescriptors * registry, LLVMTypeRef llvm_type, char const * func, int line);
#define create_fallback_descriptor(registry, llvm_type)                                                                \
    create_fallback_descriptor_impl(registry, llvm_type, __func__, __LINE__)

TypeDescriptor const * type_descriptor_get_uint64_type(TypeDescriptors * registry, bool const_qualified);

TypeDescriptor const * type_descriptor_get_uint32_type(TypeDescriptors * registry, bool const_qualified);

TypeDescriptor const * type_descriptor_get_uint8_type(TypeDescriptors * registry, bool const_qualified);

TypeDescriptor const * type_descriptor_get_int64_type(TypeDescriptors * registry, bool const_qualified);

TypeDescriptor const * type_descriptor_get_int32_type(TypeDescriptors * registry, bool const_qualified);

TypeDescriptor const * type_descriptor_get_int8_type(TypeDescriptors * registry, bool const_qualified);

TypeDescriptor const * type_descriptor_get_bool_type(TypeDescriptors * registry, bool const_qualified);

TypeDescriptor const * type_descriptor_get_void_type(TypeDescriptors * registry);
