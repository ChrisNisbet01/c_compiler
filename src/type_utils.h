#pragma once

#include "c_grammar_ast.h"
#include "struct_members.h"
#include "type_kinds.h"
#include "typed_value.h"

#include <llvm-c/Core.h>
#include <stdint.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

typedef struct TypeDescriptor_st TypeDescriptor;
typedef struct TypeDescriptors TypeDescriptors;
typedef struct scope scope_t;

char const * extract_typedef_name(c_grammar_node_t const * type_spec_node);

char const * extract_struct_or_union_or_enum_tag(c_grammar_node_t const * type_spec_node, type_kind_t * kind);

bool is_function_suffix(c_grammar_node_t const * suffix);

c_grammar_node_t const * extract_parameter_list(c_grammar_node_t const * suffix);

c_grammar_node_t const * search_parameters_list_in_declarator(c_grammar_node_t const * declarator_node);

char const * search_for_identifier(c_grammar_node_t const * node);

/**
 * @brief Checks if two function types have matching signatures.
 * @param type1 First function type.
 * @param type2 Second function type.
 * @return true if signatures match, false otherwise.
 */
bool function_signatures_match(LLVMTypeRef type1, LLVMTypeRef type2);
