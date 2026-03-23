#pragma once

#include "c_grammar_ast.h"

char const * get_node_type_name_from_node(c_grammar_node_t const * node);

char const * get_node_type_name_from_type(c_grammar_node_type_t node_type);
