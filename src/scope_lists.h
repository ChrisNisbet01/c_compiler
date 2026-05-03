#pragma once

#include "struct_bitfield_data.h"
#include "typed_value.h"

typedef struct TypeDescriptor TypeDescriptor;

#include <llvm-c/Core.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// --- Type kind for tagged types and typedef entries ---
typedef enum
{
    TYPE_KIND_BUILTIN,         // For typedefs that refer directly to builtin types (e.g., typedef int myint;)
    TYPE_KIND_STRUCT,          // Tagged struct
    TYPE_KIND_UNION,           // Tagged union
    TYPE_KIND_UNTAGGED_STRUCT, // Untagged struct (anonymous)
    TYPE_KIND_UNTAGGED_UNION,  // Untagged union (anonymous)
    TYPE_KIND_ENUM,            // Tagged enum
    TYPE_KIND_UNTAGGED_ENUM    // Untagged enum (anonymous)
} type_kind_t;

// --- Struct type info for member access ---

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

// --- Type info for tagged/untagged types ---
typedef struct type_info
{
    char * tag;       // The tag name (e.g., "MyStruct"), or "" for anonymous structs/unions
    type_kind_t kind; // TYPE_KIND_STRUCT, TYPE_KIND_UNION, or TYPE_KIND_ENUM
    TypeDescriptor const * type_desc;
    struct_field_t * fields;
    size_t field_count;
} type_info_t;

typedef struct scope_types
{
    type_info_t * entries;
    size_t count;
    size_t capacity;
} scope_types_t;

// --- Typedef entry ---
typedef struct scope_typedef_entry
{
    char * name;                      // The typedef's own name
    type_kind_t kind;                 // Which category this refers to
    TypeDescriptor const * type_desc; /* For native types, nested typedefs and enums(e.g. int, char) */
    char const * tag;                 // For tagged kinds - which entry in struct/union/enum list
    int untagged_index;               // For untagged kinds - index into untagged list, -1 otherwise
} scope_typedef_entry_t;

// --- Types (structs/unions/enums) in a scope ---
// --- Typedefs in a scope ---
typedef struct scope_typedefs
{
    scope_typedef_entry_t * entries;
    size_t count;
    size_t capacity;
} scope_typedefs_t;

// --- Symbol Table Management ---
typedef struct symbol
{
    char const * name;
    char const * tag_name; /* e.g. struct <tag> {...}; */
    TypedValue value;
} symbol_t;

typedef struct
{
    symbol_t * symbols;
    size_t count;
    size_t capacity;
} scope_symbols_t;

void scope_types_free(scope_types_t * list);

bool scope_types_init(scope_types_t * list);

type_info_t const * scope_types_add_entry(scope_types_t * list, type_info_t info);

type_info_t * scope_types_lookup_entry_by_tag_and_kind(scope_types_t const * list, char const * tag, type_kind_t kind);

type_info_t * scope_types_lookup_entry_by_type_descriptor(scope_types_t const * list, TypeDescriptor const * type_desc);

scope_typedef_entry_t * scope_typedefs_lookup_entry_by_name(scope_typedefs_t const * list, char const * name);

void scope_typedefs_free(scope_typedefs_t * list);

bool scope_typedefs_init(scope_typedefs_t * list);

void scope_typedefs_add_entry(scope_typedefs_t * list, scope_typedef_entry_t entry);

scope_typedef_entry_t *
scope_typedefs_lookup_entry_by_type_descriptor(scope_typedefs_t const * list, TypeDescriptor const * type_desc);

void scope_symbols_free(scope_symbols_t * list);

bool scope_symbols_init(scope_symbols_t * list);

symbol_t * scope_symbols_lookup_entry_by_name(scope_symbols_t const * list, char const * name);

void scope_symbols_add_entry_with_tag(scope_symbols_t * list, char const * name, TypedValue value, char const * tag);
