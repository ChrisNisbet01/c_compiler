#pragma once

#include "c_grammar_ast.h"
#include "llvm_ir_generator.h"
#include "struct_members.h"

#include <llvm-c/Core.h>

typedef struct TypeDescriptor TypeDescriptor;
typedef struct TypeDescriptors TypeDescriptors;
typedef struct scope scope_t;

typedef struct ir_generator_ctx ir_generator_ctx_t;

char const * extract_typedef_name(c_grammar_node_t const * type_spec_node);

char const * extract_struct_or_union_or_enum_tag(c_grammar_node_t const * type_spec_node);

TypeDescriptor const * find_typedef_type_descriptor(ir_generator_ctx_t * ctx, char const * name);

LLVMTypeRef find_type_by_tag(ir_generator_ctx_t * ctx, char const * name);

TypeDescriptor const * find_type_descriptor_by_tag(ir_generator_ctx_t * ctx, char const * name);

char * generate_anon_name(ir_generator_ctx_t * ctx, char const * prefix);

bool is_function_suffix(c_grammar_node_t const * suffix);

c_grammar_node_t const * extract_parameter_list(c_grammar_node_t const * suffix);

c_grammar_node_t const * search_parameters_list_in_declarator(c_grammar_node_t const * declarator_node);

int evaluate_enum_value_assignment_expression(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * value_node, int current_value
);

bool register_enum_constants(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node);

char const * search_for_identifier(c_grammar_node_t const * node);

unsigned get_fp_width(LLVMTypeRef type);
