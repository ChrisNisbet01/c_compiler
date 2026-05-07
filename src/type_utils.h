#pragma once

#include "c_grammar_ast.h"
#include "llvm_ir_generator.h"
#include "struct_members.h"

#include <llvm-c/Core.h>
#include <stdint.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

typedef struct TypeDescriptor TypeDescriptor;
typedef struct TypeDescriptors TypeDescriptors;
typedef struct scope scope_t;

typedef struct ir_generator_ctx ir_generator_ctx_t;

char const * extract_typedef_name(c_grammar_node_t const * type_spec_node);

char const * extract_struct_or_union_or_enum_tag(c_grammar_node_t const * type_spec_node);

char * generate_anon_name(ir_generator_ctx_t * ctx, char const * prefix);

bool is_function_suffix(c_grammar_node_t const * suffix);

c_grammar_node_t const * extract_parameter_list(c_grammar_node_t const * suffix);

c_grammar_node_t const * search_parameters_list_in_declarator(c_grammar_node_t const * declarator_node);

char const * search_for_identifier(c_grammar_node_t const * node);

unsigned get_fp_width(LLVMTypeRef type);

uint64_t get_type_alignment(ir_generator_ctx_t * ctx, LLVMTypeRef type);

uint64_t get_type_size(ir_generator_ctx_t * ctx, TypeDescriptor const * type);

/**
 * @brief Checks if two function types have matching signatures.
 * @param type1 First function type.
 * @param type2 Second function type.
 * @return true if signatures match, false otherwise.
 */
bool function_signatures_match(LLVMTypeRef type1, LLVMTypeRef type2);

TypedValue cast_typed_value_to_desc(ir_generator_ctx_t * ctx, TypedValue src, TypeDescriptor const * target_desc);

void aligned_store(
    ir_generator_ctx_t * ctx, LLVMBuilderRef builder, LLVMValueRef value, LLVMTypeRef value_type, LLVMValueRef ptr
);

// Helper wrapper for LLVMBuildLoad2 with proper alignment
LLVMValueRef
aligned_load(ir_generator_ctx_t * ctx, LLVMBuilderRef builder, LLVMTypeRef ty, LLVMValueRef ptr, char const * name);
