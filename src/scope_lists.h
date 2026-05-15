#pragma once

#include "generic_hash_table.h"
#include "scope_typedef.h"
#include "struct_members.h"
#include "symbols.h"
#include "type_kinds.h"
#include "typed_value.h"

typedef struct TypeDescriptor_st TypeDescriptor;

#include <llvm-c/Core.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// --- Struct type info for member access ---

typedef struct scope_types
{
    generic_hash_table_t * by_tag;
    generic_hash_table_t * by_type_desc;
    bool is_master;
} scope_types_t;

typedef struct
{
    scope_types_t tagged;
    scope_types_t type_desc;
} type_lists_t;

// --- Types (structs/unions/enums) in a scope ---
// --- Typedefs in a scope ---
typedef struct scope_typedefs
{
    generic_hash_table_t * by_name;
} scope_typedefs_t;

// --- Symbol Table Management ---

typedef struct
{
    generic_hash_table_t * by_name;
} scope_symbols_t;

void scope_type_lists_free(type_lists_t * list);

bool scope_type_lists_init(type_lists_t * list);

type_info_t const * scope_types_add_entry(type_lists_t * list, type_info_t info);

type_info_t * scope_types_lookup_entry_by_tag(type_lists_t const * list, char const * tag);

type_info_t * scope_types_lookup_entry_by_type_descriptor(type_lists_t const * list, TypeDescriptor const * type_desc);

scope_typedef_entry_t * scope_typedefs_lookup_entry_by_name(scope_typedefs_t const * list, char const * name);

void scope_typedefs_free(scope_typedefs_t * list);

bool scope_typedefs_init(scope_typedefs_t * list);

void scope_typedefs_add_entry(scope_typedefs_t * list, scope_typedef_entry_t entry);

void scope_symbols_free(scope_symbols_t * list);

bool scope_symbols_init(scope_symbols_t * list);

symbol_t * scope_symbols_lookup_entry_by_name(scope_symbols_t const * list, char const * name);

void scope_symbols_add_entry(scope_symbols_t * list, char const * name, TypedValue value);
