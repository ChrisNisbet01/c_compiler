#pragma once

#include "debug.h"
#include "struct_members.h"
#include "type_qualifiers.h"
#include "type_specifier.h"

#include <llvm-c/Core.h>
#include <llvm-c/Target.h> // For target initialization, etc.
#include <stdbool.h>
#include <stddef.h>

typedef enum
{
    NCC_TYPE_KIND_BUILTIN,
    NCC_TYPE_KIND_POINTER,
    NCC_TYPE_KIND_ARRAY,
    NCC_TYPE_KIND_STRUCT,
    NCC_TYPE_KIND_UNION,
    NCC_TYPE_KIND_FUNCTION,
} type_descriptor_type_kind_t;

typedef struct TypeDescriptors TypeDescriptors;
typedef struct TypeDescriptor_st TypeDescriptor;

typedef struct
{
    unsigned width;
} FloatMetadata;

typedef struct
{
    unsigned width;
} IntegerMetadata;

typedef struct
{
    TypeDescriptor const * return_type;
    unsigned param_count;
    TypeDescriptor const ** params; // Allocated once in the registry
    bool is_void_return;
    bool is_variadic;
} FunctionMetadata;

typedef struct
{
    bool is_complete;
    struct_or_union_members_st members;
    uint64_t total_size;
    uint32_t alignment;
} StructMetaData;

typedef struct
{
    size_t size;
} ArrayMetaData;

struct TypeDescriptor_st
{
    type_descriptor_type_kind_t kind;

    LLVMTypeRef llvm_type; /* The underlying LLVM type. */

    TypeQualifier qualifiers;
    TypeSpecifier specifiers;

    // Relationships
    TypeDescriptor const * pointee; // For pointers/arrays

    /* Function-specific */
    FunctionMetadata function_metadata;

    /* Struct/union-specific */
    StructMetaData struct_metadata;

    IntegerMetadata integer_metadata;
    FloatMetadata float_metadata;
    ArrayMetaData array_metadata;
};

TypeDescriptors * type_descriptors_create_registry(LLVMContextRef context, LLVMTargetDataRef data_layout);

void type_descriptors_destroy_registry(TypeDescriptors * registry);

TypeDescriptor const *
get_or_create_array_type(TypeDescriptors * registry, TypeDescriptor const * element_type, size_t size);

TypeDescriptor const *
get_or_create_pointer_type(TypeDescriptors * registry, TypeDescriptor const * pointee, TypeQualifier qualifiers);

/**
 * @brief Returns a type descriptor with the given qualifiers applied.
 * If no qualifiers are specified, returns the original type.
 * If qualifiers are specified, searches for an existing type with those qualifiers
 * or creates a new one.
 */
TypeDescriptor const *
get_or_create_qualified_type(TypeDescriptors * registry, TypeDescriptor const * base_type, TypeQualifier qualifiers);

TypeDescriptor const * get_or_create_builtin_type(TypeDescriptors * registry, TypeSpecifier specs, TypeQualifier quals);

TypeDescriptor const * register_struct_type(
    TypeDescriptors * registry,
    LLVMTypeRef llvm_struct,
    TypeQualifier const quals,
    bool is_union,
    bool is_complete,
    struct_or_union_members_st const * members
);

TypeDescriptor const * get_or_create_function_type(
    TypeDescriptors * registry,
    TypeDescriptor const * ret_type,
    TypeDescriptor const ** params,
    size_t param_count,
    bool is_variadic
);

TypeDescriptor const *
get_type_descriptor_from_specifiers(TypeDescriptors * registry, TypeSpecifier const specs, TypeQualifier const quals);

TypeDescriptor const * type_descriptor_get_pointee(TypeDescriptor const * desc);

TypeDescriptor const * find_descriptor_by_llvm_type(TypeDescriptors * registry, LLVMTypeRef type);

TypeDescriptor const * type_descriptor_get_uint64_type(TypeDescriptors * registry, bool const_qualified);

TypeDescriptor const * type_descriptor_get_uint32_type(TypeDescriptors * registry, bool const_qualified);

TypeDescriptor const * type_descriptor_get_uint8_type(TypeDescriptors * registry, bool const_qualified);

TypeDescriptor const * type_descriptor_get_int64_type(TypeDescriptors * registry, bool const_qualified);

TypeDescriptor const * type_descriptor_get_int32_type(TypeDescriptors * registry, bool const_qualified);

TypeDescriptor const * type_descriptor_get_int8_type(TypeDescriptors * registry, bool const_qualified);

TypeDescriptor const * type_descriptor_get_bool_type(TypeDescriptors * registry, bool const_qualified);

TypeDescriptor const * type_descriptor_get_void_type(TypeDescriptors * registry);

/**
 * Searches the struct metadata for a member with the matching name.
 * Returns the 0-based index of the field, or -1 if not found.
 */
int type_descriptor_find_struct_field_index_from_desc(TypeDescriptor const * desc, char const * name);

struct_field_t const * type_descriptor_get_struct_field_type(TypeDescriptor const * desc, int index);

bool is_integer_kind(TypeDescriptor const * desc);

bool is_floating_kind(TypeDescriptor const * desc);

bool is_void_return(TypeDescriptor const * desc);

uint32_t get_type_alignment_desc(TypeDescriptor const * desc);

uint64_t get_type_size_desc(LLVMTargetDataRef data_layout, TypeDescriptor const * desc);

void dump_type_descriptor(char const * name, TypeDescriptor const * desc, debug_level_t level);

void type_descriptor_complete_struct(
    TypeDescriptors * registry, TypeDescriptor const * type_desc_in, struct_or_union_members_st const * members
);
