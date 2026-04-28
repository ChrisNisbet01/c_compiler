#pragma once

#include "c_grammar_ast.h"
#include "llvm_ir_generator.h"

typedef struct
{
    size_t num_members;
    struct_field_t * members;
} struct_or_union_members_st;

char const * extract_typedef_name(c_grammar_node_t const * type_spec_node);

char const * extract_struct_or_union_or_enum_tag(c_grammar_node_t const * type_spec_node);

TypeDescriptor const * find_typedef_type_descriptor(ir_generator_ctx_t * ctx, char const * name);

LLVMTypeRef find_type_by_tag(ir_generator_ctx_t * ctx, char const * name);

TypeDescriptor const * find_type_descriptor_by_tag(ir_generator_ctx_t * ctx, char const * name);

char * generate_anon_name(ir_generator_ctx_t * ctx, char const * prefix);

type_info_t const * register_struct_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child);

bool is_function_suffix(c_grammar_node_t const * suffix);

c_grammar_node_t const * extract_parameter_list(c_grammar_node_t const * suffix);

c_grammar_node_t const * search_parameters_list_in_declarator(c_grammar_node_t const * declarator_node);

int evaluate_enum_value_assignment_expression(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * value_node, int current_value
);

bool register_enum_constants(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node);

char const * search_for_identifier(c_grammar_node_t const * node);
