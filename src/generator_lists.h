#pragma once

#include "llvm_ir_generator.h"
#include "scope_typedef.h"
#include "struct_members.h"
#include "symbols.h"
#include "type_descriptors.h"
#include "type_kinds.h"
#include "typed_value.h"

// --- Scope push/pop ---

/**
 * @brief Pushes a new scope onto the scope stack.
 * @param ctx The IR generator context.
 */
void generator_scope_push(ir_generator_ctx_t * ctx);

/**
 * @brief Pops the current scope from the scope stack.
 * @param ctx The IR generator context.
 */
void generator_scope_pop(ir_generator_ctx_t * ctx);

/**
 * @brief Adds a forward declaration typedef to a scope.
 * @param scope The scope to add to.
 * @param typedef_name The name of the typedef.
 * @param tag The tag name of the type being typedef'd.
 * @param kind The kind of type being typedef'd.
 */
void generator_add_typedef_forward_decl(
    ir_generator_ctx_t * ctx, char const * typedef_name, char const * tag, type_kind_t kind
);

/**
 * @brief Adds a symbol to a scope with associated struct tag.
 * @param ctx The IR generator context.
 * @param name The symbol name.
 * @param value The LLVM value metadata.
 * @param tag The struct tag name (or NULL).
 */
void generator_add_tagged_symbol(ir_generator_ctx_t * ctx, char const * name, TypedValue value, char const * tag);

/**
 * @brief Adds a symbol to a scope.
 * @param ctx The IR generator context.
 * @param name The symbol name.
 * @param value The LLVM value metadata.
 */
void generator_add_symbol(ir_generator_ctx_t * ctx, char const * name, TypedValue value);

/**
 * @brief Finds a symbol in a scope and returns its pointer and type.
 * @param ctx The IR generator context.
 * @param name The name of the symbol to find.
 * @param out_symbol Pointer to store the found sybol metadata.
 * @return True if the symbol was found, false otherwise.
 */
bool generator_lookup_symbol_value(ir_generator_ctx_t * ctx, char const * name, TypedValue * out_symbol);

/**
 * @brief Finds the struct tag name associated with a symbol.
 * @param ctx The IR generator context.
 * @param name The symbol name.
 * @return The struct tag name, or NULL if not found.
 */
char const * generator_lookup_symbol_tag_name(ir_generator_ctx_t * ctx, char const * name);

/**
 * @brief Finds a symbol entry by name and returns the full symbol struct.
 * @param ctx The IR generator context.
 * @param name The symbol name.
 * @return Pointer to the symbol struct, or NULL if not found.
 */
symbol_t const * generator_lookup_symbol_entry(ir_generator_ctx_t * ctx, char const * name);

LLVMBasicBlockRef generator_get_or_create_label(ir_generator_ctx_t * ctx, char const * label_name);

scope_typedef_entry_t * generator_lookup_typedef_entry_by_name(ir_generator_ctx_t * ctx, char const * name);

scope_typedef_entry_t *
generator_lookup_typedef_entry_by_type_descriptor(ir_generator_ctx_t * ctx, TypeDescriptor const * type_desc);

type_info_t const * generator_add_tagged_type(ir_generator_ctx_t * ctx, type_info_t info);

type_info_t const * generator_add_untagged_type(ir_generator_ctx_t * ctx, type_info_t info);

scope_t * generator_scope_create(ir_generator_ctx_t * ctx);

void generator_add_typedef_entry(ir_generator_ctx_t * ctx, scope_typedef_entry_t entry);

type_info_t * generator_lookup_type_info_by_type_descriptor(ir_generator_ctx_t * ctx, TypeDescriptor const * type_desc);

TypeDescriptor const * generator_find_typedef_type_descriptor(ir_generator_ctx_t * ctx, char const * name);

TypeDescriptor const *
generator_find_type_descriptor_by_tag_and_kind(ir_generator_ctx_t * ctx, char const * tag, type_kind_t kind);

type_info_t *
generator_lookup_tagged_entry_by_tag_and_kind(ir_generator_ctx_t * ctx, char const * tag, type_kind_t kind);
