#pragma once

#include "scope_typedef.h"
#include "struct_members.h"
#include "symbols.h"
#include "type_kinds.h"
#include "typed_value.h"

typedef struct TypeDescriptor TypeDescriptor;

#include <llvm-c/Core.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// --- Struct type info for member access ---

typedef struct scope_types
{
    type_info_t ** entries;
    size_t count;
    size_t capacity;
} scope_types_t;

// --- Types (structs/unions/enums) in a scope ---
// --- Typedefs in a scope ---
typedef struct scope_typedefs
{
    scope_typedef_entry_t ** entries;
    size_t count;
    size_t capacity;
} scope_typedefs_t;

// --- Symbol Table Management ---

typedef struct
{
    symbol_t ** symbols;
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
