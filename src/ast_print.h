#pragma once

#include "c_grammar_ast.h"

void print_ast(c_grammar_node_t const * node);

void print_ast_with_label(c_grammar_node_t const * node, char const * label);
