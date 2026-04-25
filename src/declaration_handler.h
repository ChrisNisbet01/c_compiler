#pragma once

#include "llvm_ir_generator.h"

TypeDescriptor const * resolve_type_from_ast(ir_generator_ctx_t * ctx, c_grammar_node_t const * declaration_node);
