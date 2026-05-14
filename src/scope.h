/**
 * @file scope.h
 * @brief Type and symbol table management for NCC compiler.
 *
 * This module provides structures and functions for managing:
 * - Type information (structs, unions, enums) with tagging
 * - Symbol tables with hierarchical scoping
 * - Typedef management
 * - Error and warning reporting
 *
 * The scope system supports both tagged types (with names) and untagged/anonymous types,
 * as well as typedef forwarding to either.
 */

#pragma once

#include "labels.h"
#include "scope_lists.h"
#include "struct_members.h"
#include "type_descriptors.h"
#include "typed_value.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

// Include necessary LLVM C API headers.
// These require LLVM to be installed and its include paths configured in CMake.
#include <llvm-c/Core.h>

typedef struct TypeDescriptor_st TypeDescriptor;

// --- Scope structure for hierarchical symbol tables ---
typedef struct scope
{
    LLVMContextRef context;
    LLVMBuilderRef builder;

    scope_symbols_t symbols;

    type_lists_t type_lists[TYPE_KIND_COUNT__];

    scope_typedefs_t typedefs; // Typedef names

    // --- Label management for goto statements ---
    label_list_t * labels; /* Only present in function blocks. get calls will find the nearest scope with a list. */

    struct scope * parent; // Chain to outer scope (NULL for global)
} scope_t;

// --- Scope lifecycle ---

/**
 * @brief Creates a new scope with the given parent.
 * @param parent The parent scope (NULL for global scope).
 * @return A new scope, or NULL on failure.
 */
scope_t * scope_create(scope_t * parent, LLVMContextRef context, LLVMBuilderRef builder);

/**
 * @brief Frees a scope and all its contents.
 * @param scope The scope to free.
 */
void scope_free(scope_t * scope);

// --- Type management ---

/**
 * @brief Adds a type to a scope.
 * @param scope The scope to add to.
 * @param info The type info to add.
 * @return Pointer to the added entry, or NULL on failure.
 */
type_info_t const * scope_add_type_info(scope_t * scope, type_info_t info);

// --- Tagged type lookup ---

type_info_t * scope_lookup_tagged_entry_by_tag_and_kind(scope_t const * scope, char const * tag, type_kind_t kind);

/**
 * @brief Finds a type by its LLVM type reference.
 * @param scope The scope to search.
 * @param type The LLVM type to search for.
 * @return Pointer to the type info, or NULL if not found.
 */
type_info_t * scope_lookup_type_info_by_type_descriptor(scope_t const * scope, TypeDescriptor const * const type_desc);

// --- Typedef management ---

/**
 * @brief Adds a typedef entry to a scope.
 * @param scope The scope to add to.
 * @param entry The typedef entry to add.
 */
void scope_add_typedef_entry(scope_t * scope, scope_typedef_entry_t entry);

/**
 * @brief Finds a typedef by name and returns its LLVM type.
 * @param scope The scope to search.
 * @param name The typedef name to search for.
 * @return The LLVM type of the typedef, or NULL if not found.
 */
TypeDescriptor const * scope_find_typedef_type_descriptor(scope_t const * scope, char const * name);

// --- Symbol management ---

/**
 * @brief Looks up a typedef entry by name.
 * @param scope The scope to search.
 * @param name The name to search for.
 * @return Pointer to the typedef entry, or NULL if not found.
 */
scope_typedef_entry_t * scope_lookup_typedef_entry_by_name(scope_t const * scope, char const * name);

scope_typedef_entry_t *
scope_lookup_typedef_entry_by_type_descriptor(scope_t const * scope, TypeDescriptor const * type_desc);

// --- Function declaration tracking ---

LLVMBasicBlockRef scope_get_or_create_label(scope_t const * scope, char const * label_name);

void scope_add_symbol(scope_t * scope, char const * name, TypedValue value);

symbol_t * scope_find_symbol_entry(scope_t const * scope, char const * name);
