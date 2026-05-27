#pragma once

#include "debug.h"
#include "llvm_ir_generator.h"

#include <stdbool.h>

typedef struct
{
    c_grammar_node_t const * decl_specifiers;
    c_grammar_node_t const * declarator;
} param_nodes;

typedef struct
{
    size_t count;
    bool is_variadic;
    param_nodes * nodes;
    char const ** names;
    TypeDescriptor const ** types;
} parameter_definitions_t;

TypeSpecifier build_type_specifiers(c_grammar_node_t const * spec_list);

bool type_specifier_is_valid(TypeSpecifier const spec);

TypeQualifier build_type_qualifiers(c_grammar_node_t const * qual_list);

TypeDescriptor const * resolve_type_descriptor(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * specifiers, c_grammar_node_t const * declarator
);

parameter_definitions_t extract_function_parameters(ir_generator_ctx_t * ctx, c_grammar_node_t const * params_list);

void parameter_definitions_cleanup(parameter_definitions_t * params);
