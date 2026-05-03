#pragma once

#include "c_grammar_ast.h"
#include "llvm_ir_generator.h"

int evaluate_enum_value_assignment_expression(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * value_node, int current_value
);

bool register_enum_constants(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node);
