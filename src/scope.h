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
#include "llvm_typed_value.h"
#include "struct_members.h"
#include "type_descriptors.h"
#include "type_utils.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

// Include necessary LLVM C API headers.
// These require LLVM to be installed and its include paths configured in CMake.
#include <llvm-c/Core.h>

typedef struct TypeDescriptor TypeDescriptor;

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

// --- Type info for tagged/untagged types ---
typedef struct type_info
{
    char * tag;       // The tag name (e.g., "MyStruct"), or "" for anonymous structs/unions
    type_kind_t kind; // TYPE_KIND_STRUCT, TYPE_KIND_UNION, or TYPE_KIND_ENUM
    TypeDescriptor const * type_desc;
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
    char * name;                      // The typedef's own name
    type_kind_t kind;                 // Which category this refers to
    TypeDescriptor const * type_desc; /* For native types, nested typedefs and enums(e.g. int, char) */
    char * tag;                       // For tagged kinds - which entry in struct/union/enum list
    int untagged_index;               // For untagged kinds - index into untagged list, -1 otherwise
} scope_typedef_entry_t;

// --- Typedefs in a scope ---
typedef struct scope_typedefs
{
    scope_typedef_entry_t * entries;
    size_t count;
    size_t capacity;
} scope_typedefs_t;

#define MAX_POINTER_INDIRECTION_LEVELS 8

typedef struct pointer_qualifiers_t
{
    unsigned int level;
    bool is_const[MAX_POINTER_INDIRECTION_LEVELS];    /* const at this level (index = deref level) */
    bool is_volatile[MAX_POINTER_INDIRECTION_LEVELS]; /* volatile at this level */
    bool is_const_on_pointee;                         /* true if const is on pointee type (e.g., const char *) */
} pointer_qualifiers_t;

// --- Symbol Table Management ---
typedef struct symbol
{
    char * name;
    TypedValue value;
    char * tag_name;
} symbol_t;

// --- Scope structure for hierarchical symbol tables ---
typedef struct scope
{
    LLVMContextRef context;
    LLVMBuilderRef builder;
    symbol_t * symbols;
    size_t symbol_count;
    size_t symbol_capacity;

    scope_types_t tagged_types;   // Tagged struct/union/enum types
    scope_types_t untagged_types; // Anonymous struct/union/enum types
    scope_typedefs_t typedefs;    // Typedef names

    // --- Label management for goto statements ---
    label_list_t * labels; /* Only present in function blocks. get calls will find the nearest scope with a list. */

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
scope_t * scope_create(scope_t * parent, LLVMContextRef context, LLVMBuilderRef builder);

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
 * @param untagged_index If not NULL, will be set to the index of the added type.
 * @return Pointer to the added entry, or NULL on failure.
 */
type_info_t const * scope_add_untagged_type(scope_t * scope, type_info_t info, int * untagged_index);

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

/**
 * @brief Finds a type by its LLVM type reference.
 * @param scope The scope to search.
 * @param type The LLVM type to search for.
 * @return Pointer to the type info, or NULL if not found.
 */
type_info_t * scope_find_type_by_type_descriptor(scope_t const * scope, TypeDescriptor const * const type_desc);

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
TypeDescriptor const * scope_find_typedef_type_descriptor(scope_t const * scope, char const * name);

// --- Symbol management ---

/**
 * @brief Adds a symbol to a scope with associated struct tag.
 * @param ctx The IR generator context.
 * @param name The symbol name.
 * @param value The LLVM value metadata.
 * @param tag The struct tag name (or NULL).
 */
void add_symbol_with_struct(ir_generator_ctx_t * ctx, char const * name, TypedValue value, char const * tag);

/**
 * @brief Adds a symbol to a scope.
 * @param ctx The IR generator context.
 * @param name The symbol name.
 * @param value The LLVM value metadata.
 */
void add_symbol(ir_generator_ctx_t * ctx, char const * name, TypedValue value);

/**
 * @brief Finds a symbol in a scope and returns its pointer and type.
 * @param ctx The IR generator context.
 * @param name The name of the symbol to find.
 * @param out_symbol Pointer to store the found sybol metadata.
 * @return True if the symbol was found, false otherwise.
 */
bool find_symbol(ir_generator_ctx_t * ctx, char const * name, TypedValue * out_symbol);

/**
 * @brief Finds the struct tag name associated with a symbol.
 * @param ctx The IR generator context.
 * @param name The symbol name.
 * @return The struct tag name, or NULL if not found.
 */
char const * find_symbol_tag_name(ir_generator_ctx_t * ctx, char const * name);

/**
 * @brief Finds a symbol entry by name and returns the full symbol struct.
 * @param ctx The IR generator context.
 * @param name The symbol name.
 * @return Pointer to the symbol struct, or NULL if not found.
 */
symbol_t const * find_symbol_entry(ir_generator_ctx_t * ctx, char const * name);

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

type_kind_t scope_lookup_kind_by_type_descriptor(scope_t const * scope, TypeDescriptor const * type_desc);

// --- Function declaration tracking ---

/**
 * @brief Adds or updates a function declaration in the context.
 * @param ctx The IR generator context.
 * @param name The function name.
 * @param type The function type.
 * @param has_definition True if this includes a function body.
 * @return true if a conflict was detected, false otherwise.
 */
bool add_function_declaration_impl(
    ir_generator_ctx_t * ctx, char const * name, TypedValue func, bool has_definition, int line
);
#define add_function_declaration(c, n, f, hd) add_function_declaration_impl((c), (n), (f), (hd), __LINE__)
/**
 * @brief Finds a function declaration by name.
 * @param ctx The IR generator context.
 * @param name The function name.
 * @return Pointer to the function declaration entry, or NULL if not found.
 */
struct function_decl_entry * find_function_declaration(ir_generator_ctx_t * ctx, char const * name);

/**
 * @brief Checks if two function types have matching signatures.
 * @param type1 First function type.
 * @param type2 Second function type.
 * @return true if signatures match, false otherwise.
 */
bool function_signatures_match(LLVMTypeRef type1, LLVMTypeRef type2);

LLVMBasicBlockRef scope_get_or_create_label(scope_t const * scope, char const * label_name);
