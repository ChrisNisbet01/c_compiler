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
 * @brief Finds a symbol entry by name and returns the full symbol struct.
 * @param ctx The IR generator context.
 * @param name The symbol name.
 * @return Pointer to the symbol struct, or NULL if not found.
 */
symbol_t const * generator_lookup_symbol_entry(ir_generator_ctx_t * ctx, char const * name);

LLVMBasicBlockRef generator_get_or_create_label(ir_generator_ctx_t * ctx, char const * label_name);

scope_typedef_entry_t * generator_lookup_typedef_entry_by_name(ir_generator_ctx_t * ctx, char const * name);

type_info_t const * generator_add_type_info(ir_generator_ctx_t * ctx, type_info_t info);

scope_t * generator_scope_create(ir_generator_ctx_t * ctx);

void generator_add_typedef_entry(ir_generator_ctx_t * ctx, scope_typedef_entry_t entry);

type_info_t * generator_lookup_type_info_by_type_descriptor(ir_generator_ctx_t * ctx, TypeDescriptor const * type_desc);

TypeDescriptor const * generator_find_typedef_type_descriptor(ir_generator_ctx_t * ctx, char const * name);

TypeDescriptor const *
generator_find_type_descriptor_by_tag_and_kind(ir_generator_ctx_t * ctx, char const * tag, type_kind_t kind);

type_info_t *
generator_lookup_tagged_entry_by_tag_and_kind(ir_generator_ctx_t * ctx, char const * tag, type_kind_t kind);
