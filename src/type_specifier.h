#pragma once

#include "c_grammar_ast.h"
#include "debug.h"

#include <stdbool.h>

typedef struct
{
    bool is_unsigned;
    bool is_signed;
    int long_count; // 0 = int, 1 = long, 2 = long long
    bool is_void;
    bool is_bool;
    bool is_short;
    bool is_char;
    bool is_int;
    bool is_float;
    bool is_double;
} TypeSpecifier;

typedef struct
{
    bool is_valid;
    bool is_native_type;
    bool is_struct_or_union_or_enum;
} TypeSpecifierValidationResult;

void type_specifier_dump(TypeSpecifier spec, debug_level_t level);

bool type_specifier_is_valid(TypeSpecifier const spec);

TypeSpecifier build_type_specifiers(c_grammar_node_t const * spec_list);

TypeSpecifierValidationResult validate_type_specifiers(c_grammar_node_t const * declaration_specifiers);

TypeSpecifier build_type_specifiers_from_declaration_specifiers(c_grammar_node_t const * declaration_specs);
