#pragma once

#include "c_grammar_ast.h"
#include "llvm_ir_generator.h"
#include "typed_value.h"

#include <llvm-c/Core.h>

// Enumeration for binary operation types
typedef enum
{
    BINARY_OP_BITWISE,
    BINARY_OP_SHIFT,
    BINARY_OP_ARITHMETIC,
    BINARY_OP_RELATIONAL,
    BINARY_OP_EQUALITY,
    BINARY_OP_COMPOUND_ASSIGNMENT,
    BINARY_OP_COUNT // Keep this last
} binary_operation_type_t;

// Generic binary expression processor
TypedValue
process_binary_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node, binary_operation_type_t op_type);

/* Use when the LHS of the expression has already been calculated. */
TypedValue complete_binary_expression(
    ir_generator_ctx_t * ctx, TypedValue lhs_res, c_grammar_node_t const * node, binary_operation_type_t op_type
);
