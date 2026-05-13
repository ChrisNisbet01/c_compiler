#pragma once

#include "c_grammar_ast.h"
#include "llvm_ir_generator.h"

bool register_enum_constants(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node);
