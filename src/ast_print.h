#pragma once

#include <stdio.h>
#include "c_grammar_ast.h"

void print_ast(c_grammar_node_t const * node);
void print_ast_to_stream(c_grammar_node_t const * node, FILE * stream);

void print_ast_with_label(c_grammar_node_t const * node, char const * label);
void print_ast_with_label_to_stream(c_grammar_node_t const * node, char const * label, FILE * stream);
