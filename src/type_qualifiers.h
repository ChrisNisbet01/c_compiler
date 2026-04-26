#pragma once

#include "c_grammar_ast.h"
#include "debug.h"

#include <stdbool.h>

typedef struct
{
    bool is_const;
    bool is_volatile;
} TypeQualifier;

TypeQualifier build_type_qualifiers(c_grammar_node_t const * qual_list);

void type_qualifiers_dump(TypeQualifier quals, debug_level_t level);
