/**
 * @file debug.c
 * @brief Debug output utilities implementation.
 */

#include "debug.h"

#include "c_grammar_ast.h"

#include <stdarg.h>
#include <stdio.h>

static debug_level_t current_level = DEBUG_LEVEL_OFF;

void
debug_set_level(debug_level_t level)
{
    current_level = level;
}

debug_level_t
debug_get_level(void)
{
    return current_level;
}

bool
debug_is_enabled(debug_level_t level)
{
    return current_level > DEBUG_LEVEL_OFF && level >= current_level;
}

void
debug_info(char const * fmt, ...)
{
    if (!debug_is_enabled(DEBUG_LEVEL_INFO))
    {
        return;
    }

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "INFO: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void
debug_warning(char const * fmt, ...)
{
    if (!debug_is_enabled(DEBUG_LEVEL_WARNING))
    {
        return;
    }

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "WARNING: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void
debug_error_int(char const * func, int line, char const * fmt, ...)
{
    if (!debug_is_enabled(DEBUG_LEVEL_ERROR))
    {
        return;
    }

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "ERROR: %s:%u\n\t", func, line);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

#include <stddef.h>
#include <stdint.h>

#define myoffsetof(t, m) ((unsigned long)&((t *)0)->m)

void
debug_dump_c_grammar_node_info(void)
{
    fprintf(stderr, "\n=== c_grammar_node_t structure analysis ===\n");
    fprintf(stderr, "sizeof(c_grammar_node_t): %zu bytes\n", sizeof(struct c_grammar_node_t));
    fprintf(stderr, "\nField offsets:\n");

    fprintf(
        stderr,
        "  type:                  offset %zu, size %zu\n",
        myoffsetof(struct c_grammar_node_t, type),
        sizeof(((struct c_grammar_node_t *)0)->type)
    );
    fprintf(
        stderr,
        "  source_data:           offset %zu, size %zu\n",
        myoffsetof(struct c_grammar_node_t, source_data),
        sizeof(((struct c_grammar_node_t *)0)->source_data)
    );
    fprintf(
        stderr,
        "  list:                  offset %zu, size %zu\n",
        myoffsetof(struct c_grammar_node_t, list),
        sizeof(((struct c_grammar_node_t *)0)->list)
    );
    fprintf(
        stderr,
        "  text:                  offset %zu, size %zu\n",
        myoffsetof(struct c_grammar_node_t, text),
        sizeof(((struct c_grammar_node_t *)0)->text)
    );
    fprintf(stderr, "  [union starts here]    offset %zu\n", myoffsetof(struct c_grammar_node_t, expression));

    fprintf(stderr, "\nUnion member offsets (approximate):\n");

    fprintf(
        stderr,
        "  expression:            offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, expression),
        sizeof(((struct c_grammar_node_t *)0)->expression)
    );
    fprintf(
        stderr,
        "  float_lit:            offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, float_lit),
        sizeof(((struct c_grammar_node_t *)0)->float_lit)
    );
    fprintf(
        stderr,
        "  integer_lit:          offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, integer_lit),
        sizeof(((struct c_grammar_node_t *)0)->integer_lit)
    );
    fprintf(
        stderr,
        "  translation_unit:      offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, translation_unit),
        sizeof(((struct c_grammar_node_t *)0)->translation_unit)
    );
    fprintf(
        stderr,
        "  external_declaration: offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, external_declaration),
        sizeof(((struct c_grammar_node_t *)0)->external_declaration)
    );
    fprintf(
        stderr,
        "  function_definition:   offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, function_definition),
        sizeof(((struct c_grammar_node_t *)0)->function_definition)
    );
    fprintf(
        stderr,
        "  declaration:          offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, declaration),
        sizeof(((struct c_grammar_node_t *)0)->declaration)
    );
    fprintf(
        stderr,
        "  decl_specifiers:       offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, decl_specifiers),
        sizeof(((struct c_grammar_node_t *)0)->decl_specifiers)
    );
    fprintf(
        stderr,
        "  top_level_declaration: offset, size: %zu %zu\n",
        myoffsetof(struct c_grammar_node_t, top_level_declaration),
        sizeof(((struct c_grammar_node_t *)0)->top_level_declaration)
    );
    fprintf(
        stderr,
        "  line_marker:           offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, line_marker),
        sizeof(((struct c_grammar_node_t *)0)->line_marker)
    );
    fprintf(
        stderr,
        "  struct_declaration:    offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, struct_declaration),
        sizeof(((struct c_grammar_node_t *)0)->struct_declaration)
    );
    fprintf(
        stderr,
        "  labeled_statement:     offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, labeled_statement),
        sizeof(((struct c_grammar_node_t *)0)->labeled_statement)
    );
    fprintf(
        stderr,
        "  if_statement:          offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, if_statement),
        sizeof(((struct c_grammar_node_t *)0)->if_statement)
    );
    fprintf(
        stderr,
        "  switch_case:          offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, switch_case),
        sizeof(((struct c_grammar_node_t *)0)->switch_case)
    );
    fprintf(
        stderr,
        "  switch_statement:     offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, switch_statement),
        sizeof(((struct c_grammar_node_t *)0)->switch_statement)
    );
    fprintf(
        stderr,
        "  type_ref:              offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, type_ref),
        sizeof(((struct c_grammar_node_t *)0)->type_ref)
    );
    fprintf(
        stderr,
        "  while_statement:      offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, while_statement),
        sizeof(((struct c_grammar_node_t *)0)->while_statement)
    );
    fprintf(
        stderr,
        "  do_while_statement:    offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, do_while_statement),
        sizeof(((struct c_grammar_node_t *)0)->do_while_statement)
    );
    fprintf(
        stderr,
        "  for_statement:        offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, for_statement),
        sizeof(((struct c_grammar_node_t *)0)->for_statement)
    );
    fprintf(
        stderr,
        "  goto_statement:        offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, goto_statement),
        sizeof(((struct c_grammar_node_t *)0)->goto_statement)
    );
    fprintf(
        stderr,
        "  return_statement:      offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, return_statement),
        sizeof(((struct c_grammar_node_t *)0)->return_statement)
    );
    fprintf(
        stderr,
        "  expression_statement:  offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, expression_statement),
        sizeof(((struct c_grammar_node_t *)0)->expression_statement)
    );
    fprintf(
        stderr,
        "  init_declarator:       offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, init_declarator),
        sizeof(((struct c_grammar_node_t *)0)->init_declarator)
    );
    fprintf(
        stderr,
        "  declarator:           offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, declarator),
        sizeof(((struct c_grammar_node_t *)0)->declarator)
    );
    fprintf(
        stderr,
        "  typedef_declarator:    offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, typedef_declarator),
        sizeof(((struct c_grammar_node_t *)0)->typedef_declarator)
    );
    fprintf(
        stderr,
        "  typedef_direct_decl:   offset, size: %zu %zu\n",
        myoffsetof(struct c_grammar_node_t, typedef_direct_declarator),
        sizeof(((struct c_grammar_node_t *)0)->typedef_direct_declarator)
    );
    fprintf(
        stderr,
        "  identifier:           offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, identifier),
        sizeof(((struct c_grammar_node_t *)0)->identifier)
    );
    fprintf(
        stderr,
        "  compound_literal:     offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, compound_literal),
        sizeof(((struct c_grammar_node_t *)0)->compound_literal)
    );
    fprintf(
        stderr,
        "  postfix_expression:   offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, postfix_expression),
        sizeof(((struct c_grammar_node_t *)0)->postfix_expression)
    );
    fprintf(
        stderr,
        "  initializer_list_entry: offset, size: %zu %zu\n",
        myoffsetof(struct c_grammar_node_t, initializer_list_entry),
        sizeof(((struct c_grammar_node_t *)0)->initializer_list_entry)
    );
    fprintf(
        stderr,
        "  struct_definition:     offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, struct_definition),
        sizeof(((struct c_grammar_node_t *)0)->struct_definition)
    );
    fprintf(
        stderr,
        "  enum_definition:      offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, enum_definition),
        sizeof(((struct c_grammar_node_t *)0)->enum_definition)
    );
    fprintf(
        stderr,
        "  function_pointer_decl: offset, %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, function_pointer_declarator),
        sizeof(((struct c_grammar_node_t *)0)->function_pointer_declarator)
    );
    fprintf(
        stderr,
        "  enumerator:           offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, enumerator),
        sizeof(((struct c_grammar_node_t *)0)->enumerator)
    );
    fprintf(
        stderr,
        "  unary_expression_pre: offset, size: %zu %zu\n",
        myoffsetof(struct c_grammar_node_t, unary_expression_prefix),
        sizeof(((struct c_grammar_node_t *)0)->unary_expression_prefix)
    );
    fprintf(
        stderr,
        "  cast_expression:      offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, cast_expression),
        sizeof(((struct c_grammar_node_t *)0)->cast_expression)
    );
    fprintf(
        stderr,
        "  binary_expression:    offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, binary_expression),
        sizeof(((struct c_grammar_node_t *)0)->binary_expression)
    );
    fprintf(
        stderr,
        "  ternary_operation:    offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, ternary_operation),
        sizeof(((struct c_grammar_node_t *)0)->ternary_operation)
    );
    fprintf(
        stderr,
        "  conditional_expr:     offset, size: %zu %zu\n",
        myoffsetof(struct c_grammar_node_t, conditional_expression),
        sizeof(((struct c_grammar_node_t *)0)->conditional_expression)
    );
    fprintf(
        stderr,
        "  op:                   offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, op),
        sizeof(((struct c_grammar_node_t *)0)->op)
    );
    fprintf(
        stderr,
        "  type_name:            offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, type_name),
        sizeof(((struct c_grammar_node_t *)0)->type_name)
    );
    fprintf(
        stderr,
        "  storage_class:       offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, storage_class),
        sizeof(((struct c_grammar_node_t *)0)->storage_class)
    );
    fprintf(
        stderr,
        "  typedef_spec_qual:    offset, size: %zu %zu\n",
        myoffsetof(struct c_grammar_node_t, typedef_specifier_qualifier),
        sizeof(((struct c_grammar_node_t *)0)->typedef_specifier_qualifier)
    );
    fprintf(
        stderr,
        "  va_arg_expression:    offset %zu, size: %zu\n",
        myoffsetof(struct c_grammar_node_t, va_arg_expression),
        sizeof(((struct c_grammar_node_t *)0)->va_arg_expression)
    );

    fprintf(stderr, "\n=== End c_grammar_node_t analysis ===\n\n");

    fprintf(stderr, "\n=== Float literal data type analysis ===\n\n");

    fprintf(stderr, "  sizeof long double: %zu bytes\n", sizeof(long double));

    fprintf(
        stderr,
        "  float literal data value:    offset %zu, size: %zu\n",
        myoffsetof(float_literal_data_t, value),
        sizeof(((float_literal_data_t *)0)->value)
    );

    fprintf(
        stderr,
        "  float literal data type:    offset %zu, size: %zu\n",
        myoffsetof(float_literal_data_t, type),
        sizeof(((float_literal_data_t *)0)->type)
    );
}
