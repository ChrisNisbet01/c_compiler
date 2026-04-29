#pragma once

#include "debug.h"
#include "llvm_ir_generator.h"

#include <stdbool.h>

TypeSpecifier build_type_specifiers(c_grammar_node_t const * spec_list);

bool type_specifier_is_valid(TypeSpecifier const spec);

void type_specifier_dump(TypeSpecifier spec, debug_level_t level);

TypeQualifier build_type_qualifiers(c_grammar_node_t const * qual_list);

TypeDescriptor const * resolve_type_descriptor(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * specifiers, c_grammar_node_t const * declarator
);

struct_or_union_members_st
extract_struct_or_union_members_type_descriptor(ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child);
