#pragma once

#include "c_grammar_ast.h"
#include "llvm_ir_generator.h"
#include "typed_value.h"

TypedValue process_unary_expression_prefix(ir_generator_ctx_t * ctx, c_grammar_node_t const * node);