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

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

// Include necessary LLVM C API headers.
// These require LLVM to be installed and its include paths configured in CMake.
#include <llvm-c/Core.h>

typedef LLVMValueRef LLVMValueRef;
typedef LLVMTypeRef LLVMTypeRef;

// --- Struct type info for member access ---
typedef struct struct_field
{
    char * name;
    LLVMTypeRef type;
    unsigned bit_offset;
    unsigned bit_width;     // bit_width == 0 indicates this is not a bitfield or an unnamed bitfield
    unsigned storage_index; // -1 for regular fields, >=0 for bitfields = index of storage field
} struct_field_t;

// --- Type kind for tagged types and typedef entries ---
typedef enum
{
    TYPE_KIND_UNKNOWN,         // Unassigned/unknown type kind
    TYPE_KIND_STRUCT,          // Tagged struct
    TYPE_KIND_UNION,           // Tagged union
    TYPE_KIND_UNTAGGED_STRUCT, // Untagged struct (anonymous)
    TYPE_KIND_UNTAGGED_UNION,  // Untagged union (anonymous)
    TYPE_KIND_ENUM,            // Tagged enum
    TYPE_KIND_UNTAGGED_ENUM    // Untagged enum (anonymous)
} type_kind_t;

// --- Type info for tagged/untagged types ---
typedef struct type_info
{
    char * tag;       // The tag name (e.g., "MyStruct"), or "" for anonymous structs/unions
    type_kind_t kind; // TYPE_KIND_STRUCT, TYPE_KIND_UNION, or TYPE_KIND_ENUM
    LLVMTypeRef type;
    LLVMTypeKind llvm_type_kind; // Cache the LLVM type kind for quick checks during member access
    struct_field_t * fields;
    size_t field_count;
} type_info_t;

// --- Types (structs/unions/enums) in a scope ---
typedef struct scope_types
{
    type_info_t * entries;
    size_t count;
    size_t capacity;
} scope_types_t;

// --- Typedef entry ---
typedef struct scope_typedef_entry
{
    char * name;        // The typedef's own name
    type_kind_t kind;   // Which category this refers to
    LLVMTypeRef type;   // Only used for non-struct kinds (e.g., primitives)
    char * tag;         // For tagged kinds - which entry in struct/union/enum list
    int untagged_index; // For untagged kinds - index into untagged list, -1 otherwise
} scope_typedef_entry_t;

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
    char * name;
    LLVMValueRef ptr;
    LLVMTypeRef type;
    LLVMTypeRef pointee_type; // For pointer types, stores the pointed-to type (e.g., for int* this would be i32)
    char * tag_name;          // For pointer-to-struct types, stores the struct tag for member access
} symbol_t;

// --- Scope structure for hierarchical symbol tables ---
typedef struct scope
{
    symbol_t * symbols;
    size_t symbol_count;
    size_t symbol_capacity;

    scope_types_t tagged_types;   // Tagged struct/union/enum types
    scope_types_t untagged_types; // Anonymous struct/union/enum types
    scope_typedefs_t typedefs;    // Typedef names

    struct scope * parent; // Chain to outer scope (NULL for global)
} scope_t;

// Forward declaration for context used by scope_push/scope_pop
typedef struct ir_generator_ctx ir_generator_ctx_t;

// --- Scope lifecycle ---

/**
 * @brief Creates a new scope with the given parent.
 * @param parent The parent scope (NULL for global scope).
 * @return A new scope, or NULL on failure.
 */
scope_t * scope_create(scope_t * parent);

/**
 * @brief Frees a scope and all its contents.
 * @param scope The scope to free.
 */
void scope_free(scope_t * scope);

// --- Type management ---

/**
 * @brief Frees the contents of a type_info_t structure.
 * @param info The type info to free.
 */
void free_type_info(type_info_t * info);

/**
 * @brief Adds a type_info to a scope_types_t list.
 * @param list The list to add to.
 * @param info The type info to add.
 * @return Pointer to the added entry, or NULL on failure.
 */
type_info_t const * add_info_to_list(scope_types_t * list, type_info_t info);

/**
 * @brief Adds a tagged type to a scope.
 * @param scope The scope to add to.
 * @param info The type info to add.
 * @return Pointer to the added entry, or NULL on failure.
 */
type_info_t const * scope_add_tagged_type(scope_t * scope, type_info_t info);

/**
 * @brief Adds an untagged type to a scope.
 * @param scope The scope to add to.
 * @param info The type info to add.
 * @return Pointer to the added entry, or NULL on failure.
 */
type_info_t const * scope_add_untagged_type(scope_t * scope, type_info_t info);

// --- Tagged type lookup ---

/**
 * @brief Finds a tagged type by tag name.
 * @param scope The scope to search (and parent scopes).
 * @param tag The tag name to search for.
 * @param kind The expected type kind.
 * @return Pointer to the type info, or NULL if not found.
 */
type_info_t * scope_find_tagged_type(scope_t const * scope, char const * tag, type_kind_t kind);

/**
 * @brief Finds a tagged struct by tag name.
 * @param scope The scope to search.
 * @param tag The tag name to search for.
 * @return Pointer to the type info, or NULL if not found.
 */
type_info_t * scope_find_tagged_struct(scope_t const * scope, char const * tag);

/**
 * @brief Finds a tagged union by tag name.
 * @param scope The scope to search.
 * @param tag The tag name to search for.
 * @return Pointer to the type info, or NULL if not found.
 */
type_info_t * scope_find_tagged_union(scope_t const * scope, char const * tag);

/**
 * @brief Finds a tagged enum by tag name.
 * @param scope The scope to search.
 * @param tag The tag name to search for.
 * @return Pointer to the type info, or NULL if not found.
 */
type_info_t * scope_find_tagged_enum(scope_t const * scope, char const * tag);

// --- Untagged type lookup ---

/**
 * @brief Finds an untagged type by index.
 * @param scope The scope to search.
 * @param kind The expected type kind.
 * @param index The index into the untagged types list.
 * @return Pointer to the type info, or NULL if not found.
 */
type_info_t const * scope_find_untagged_type(scope_t const * scope, type_kind_t kind, int index);

/**
 * @brief Finds an untagged struct by index.
 * @param scope The scope to search.
 * @param index The index into the untagged types list.
 * @return Pointer to the type info, or NULL if not found.
 */
type_info_t const * scope_find_untagged_struct(scope_t const * scope, int index);

/**
 * @brief Finds an untagged union by index.
 * @param scope The scope to search.
 * @param index The index into the untagged types list.
 * @return Pointer to the type info, or NULL if not found.
 */
type_info_t const * scope_find_untagged_union(scope_t const * scope, int index);

/**
 * @brief Finds an untagged enum by index.
 * @param scope The scope to search.
 * @param index The index into the untagged types list.
 * @return Pointer to the type info, or NULL if not found.
 */
type_info_t const * scope_find_untagged_enum(scope_t const * scope, int index);

// --- LLVM type lookup ---

/**
 * @brief Finds a type by its LLVM type reference.
 * @param scope The scope to search.
 * @param type The LLVM type to search for.
 * @return Pointer to the type info, or NULL if not found.
 */
type_info_t * scope_find_type_by_llvm_type(scope_t const * scope, LLVMTypeRef type);

// --- Typedef management ---

/**
 * @brief Adds a typedef entry to a scope.
 * @param scope The scope to add to.
 * @param entry The typedef entry to add.
 */
void scope_add_typedef_entry(scope_t * scope, scope_typedef_entry_t entry);

/**
 * @brief Adds a forward declaration typedef to a scope.
 * @param scope The scope to add to.
 * @param typedef_name The name of the typedef.
 * @param tag The tag name of the type being typedef'd.
 * @param kind The kind of type being typedef'd.
 */
void scope_add_typedef_forward_decl(scope_t * scope, char const * typedef_name, char const * tag, type_kind_t kind);

/**
 * @brief Finds a typedef by name and returns its LLVM type.
 * @param scope The scope to search.
 * @param name The typedef name to search for.
 * @return The LLVM type of the typedef, or NULL if not found.
 */
LLVMTypeRef scope_find_typedef(scope_t const * scope, char const * name);

// --- Symbol management ---

/**
 * @brief Adds a symbol to a scope with associated struct tag.
 * @param ctx The IR generator context.
 * @param name The symbol name.
 * @param ptr The LLVM value.
 * @param type The LLVM type.
 * @param pointee_type The pointed-to type (for pointers).
 * @param tag The struct tag name (or NULL).
 */
void add_symbol_with_struct(
    ir_generator_ctx_t * ctx,
    char const * name,
    LLVMValueRef ptr,
    LLVMTypeRef type,
    LLVMTypeRef pointee_type,
    char const * tag
);

/**
 * @brief Adds a symbol to a scope.
 * @param ctx The IR generator context.
 * @param name The symbol name.
 * @param ptr The LLVM value.
 * @param type The LLVM type.
 * @param pointee_type The pointed-to type (for pointers).
 */
void
add_symbol(ir_generator_ctx_t * ctx, char const * name, LLVMValueRef ptr, LLVMTypeRef type, LLVMTypeRef pointee_type);

/**
 * @brief Finds a symbol in a scope and returns its pointer and type.
 * @param ctx The IR generator context.
 * @param name The name of the symbol to find.
 * @param out_ptr Pointer to store the found LLVMValueRef.
 * @param out_type Pointer to store the found LLVMTypeRef.
 * @param out_pointee_type Pointer to store the found pointed-to type.
 * @return True if the symbol was found, false otherwise.
 */
bool find_symbol(
    ir_generator_ctx_t * ctx,
    char const * name,
    LLVMValueRef * out_ptr,
    LLVMTypeRef * out_type,
    LLVMTypeRef * out_pointee_type
);

/**
 * @brief Finds the struct tag name associated with a symbol.
 * @param ctx The IR generator context.
 * @param name The symbol name.
 * @return The struct tag name, or NULL if not found.
 */
char const * find_symbol_tag_name(ir_generator_ctx_t * ctx, char const * name);

// --- Scope push/pop ---

/**
 * @brief Pushes a new scope onto the scope stack.
 * @param ctx The IR generator context.
 */
void scope_push(ir_generator_ctx_t * ctx);

/**
 * @brief Pops the current scope from the scope stack.
 * @param ctx The IR generator context.
 */
void scope_pop(ir_generator_ctx_t * ctx);

// --- Internal lookup helpers (for use within scope.c) ---

/**
 * @brief Looks up a tagged entry by tag name (internal helper).
 * @param scope The scope to search.
 * @param tag The tag name to search for.
 * @return Pointer to the type info, or NULL if not found.
 */
type_info_t * scope_lookup_tagged_entry_by_tag(scope_t const * scope, char const * tag);

/**
 * @brief Looks up an untagged entry by index (internal helper).
 * @param scope The scope to search.
 * @param index The index to search for.
 * @return Pointer to the type info, or NULL if not found.
 */
type_info_t * scope_lookup_untagged_entry_by_index(scope_t const * scope, int index);

/**
 * @brief Looks up a typedef entry by name (internal helper).
 * @param scope The scope to search.
 * @param name The name to search for.
 * @return Pointer to the typedef entry, or NULL if not found.
 */
scope_typedef_entry_t * scope_lookup_typedef_entry_by_name(scope_t const * scope, char const * name);
