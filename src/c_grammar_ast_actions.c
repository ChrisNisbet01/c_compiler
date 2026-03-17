#include "c_grammar_ast_actions.h"
#include "c_grammar_ast.h"
#include "c_grammar_actions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void c_grammar_node_free(void *node_ptr, void *user_data)
{
    (void)user_data;
    c_grammar_node_t *node = (c_grammar_node_t *)node_ptr;
    if (node == NULL)
    {
        return;
    }

    switch (node->type)
    {
    case AST_NODE_TRANSLATION_UNIT:
    case AST_NODE_FUNCTION_DEFINITION:
    case AST_NODE_COMPOUND_STATEMENT:
    case AST_NODE_DECLARATION:
    case AST_NODE_DECL_SPECIFIERS:
    case AST_NODE_ASSIGNMENT:
    case AST_NODE_BINARY_OP:
    case AST_NODE_UNARY_OP:
    case AST_NODE_DECLARATOR:
    case AST_NODE_DIRECT_DECLARATOR:
    case AST_NODE_DECLARATOR_SUFFIX:
    case AST_NODE_RELATIONAL_EXPRESSION:
    case AST_NODE_EQUALITY_EXPRESSION:
    case AST_NODE_AND_EXPRESSION:
    case AST_NODE_EXCLUSIVE_OR_EXPRESSION:
    case AST_NODE_INCLUSIVE_OR_EXPRESSION:
    case AST_NODE_LOGICAL_AND_EXPRESSION:
    case AST_NODE_LOGICAL_OR_EXPRESSION:
    case AST_NODE_FUNCTION_CALL:
    case AST_NODE_ARRAY_SUBSCRIPT:
    case AST_NODE_MEMBER_ACCESS_DOT:
    case AST_NODE_MEMBER_ACCESS_ARROW:
    case AST_NODE_CAST_EXPRESSION:
    case AST_NODE_INIT_DECLARATOR:
    case AST_NODE_IF_STATEMENT:
    case AST_NODE_SWITCH_STATEMENT:
    case AST_NODE_WHILE_STATEMENT:
    case AST_NODE_DO_WHILE_STATEMENT:
    case AST_NODE_FOR_STATEMENT:
    case AST_NODE_GOTO_STATEMENT:
    case AST_NODE_CONTINUE_STATEMENT:
    case AST_NODE_BREAK_STATEMENT:
    case AST_NODE_RETURN_STATEMENT:
        if (node->data.list.children != NULL)
        {
            for (size_t i = 0; i < node->data.list.count; i++)
            {
                c_grammar_node_free(node->data.list.children[i], user_data);
            }
            free(node->data.list.children);
        }
        break;
    case AST_NODE_INTEGER_LITERAL:
    case AST_NODE_IDENTIFIER:
    case AST_NODE_TYPE_SPECIFIER:
    case AST_NODE_OPERATOR:
    case AST_NODE_POINTER:
        free(node->data.terminal.text);
        break;
    }
    free(node);
}

static c_grammar_node_t *
create_list_node(c_grammar_node_type_t type, void **children, int count)
{
    c_grammar_node_t *node = calloc(1, sizeof(*node));
    if (node == NULL)
    {
        return NULL;
    }
    node->type = type;
    node->data.list.count = (size_t)count;
    if (count > 0)
    {
        node->data.list.children = calloc((size_t)count, sizeof(*node->data.list.children));
        if (node->data.list.children == NULL)
        {
            free(node);
            return NULL;
        }
        for (int i = 0; i < count; i++)
        {
            node->data.list.children[i] = (c_grammar_node_t *)children[i];
        }
    }

    return node;
}

static c_grammar_node_t *
create_terminal_node(c_grammar_node_type_t type, epc_cpt_node_t *node)
{
    c_grammar_node_t *ast_node = calloc(1, sizeof(*ast_node));
    if (ast_node == NULL)
    {
        return NULL;
    }
    ast_node->type = type;
    const char *text = epc_cpt_node_get_semantic_content(node);
    ast_node->data.terminal.text = strndup(text, epc_cpt_node_get_semantic_len(node));
    return ast_node;
}

/* --- Semantic Action Callbacks --- */

static void
handle_list_node(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data,
    c_grammar_node_type_t type)
{
    (void)node;

    c_grammar_node_t *ast_node = create_list_node(type, children, count);
    if (ast_node == NULL)
    {
        for (int i = 0; i < count; i++)
        {
            c_grammar_node_free(children[i], user_data);
        }
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_translation_unit(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_TRANSLATION_UNIT);
}

static void
handle_function_definition(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_FUNCTION_DEFINITION);
}

static void
handle_compound_statement(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_COMPOUND_STATEMENT);
}

static void
handle_declaration(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_DECLARATION);
}

static void
handle_integer_literal(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    if (count > 0)
    {
        for (int i = 0; i < count; i++)
        {
            c_grammar_node_free(children[i], user_data);
        }
        epc_ast_builder_set_error(ctx, "Integer literal expected no children, but got %u", count);
        return;
    }

    c_grammar_node_t *ast_node = create_terminal_node(AST_NODE_INTEGER_LITERAL, node);
    if (ast_node == NULL)
    {
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    ast_node->data.terminal.value = strtol(ast_node->data.terminal.text, NULL, 0);
    epc_ast_push(ctx, ast_node);
}

static void
handle_identifier(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    if (count > 0)
    {
        for (int i = 0; i < count; i++)
        {
            c_grammar_node_free(children[i], user_data);
        }
        epc_ast_builder_set_error(ctx, "Identifier expected no children, but got %u", count);
        return;
    }

    c_grammar_node_t *ast_node = create_terminal_node(AST_NODE_IDENTIFIER, node);
    if (ast_node == NULL)
    {
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_decl_specifiers(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_DECL_SPECIFIERS);
}

static void
handle_assignment(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_ASSIGNMENT);
}

static void
handle_type_specifier(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    if (count > 0)
    {
        for (int i = 0; i < count; i++)
        {
            c_grammar_node_free(children[i], user_data);
        }
        epc_ast_builder_set_error(ctx, "Type specifier expected no children, but got %u", count);
        return;
    }

    c_grammar_node_t *ast_node = create_terminal_node(AST_NODE_TYPE_SPECIFIER, node);
    if (ast_node == NULL)
    {
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_binary_op(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_BINARY_OP);
}

static void
handle_unary_op(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_UNARY_OP);
}

static void
handle_operator(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    if (count > 0)
    {
        for (int i = 0; i < count; i++)
        {
            c_grammar_node_free(children[i], user_data);
        }
        epc_ast_builder_set_error(ctx, "Operator expected no children, but got %u", count);
        return;
    }

    c_grammar_node_t *ast_node = create_terminal_node(AST_NODE_OPERATOR, node);
    if (ast_node == NULL)
    {
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_declarator(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_DECLARATOR);
}

static void
handle_direct_declarator(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_DIRECT_DECLARATOR);
}

static void
handle_declarator_suffix(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_DECLARATOR_SUFFIX);
}

static void
handle_pointer(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    if (count > 0)
    {
        for (int i = 0; i < count; i++)
        {
            c_grammar_node_free(children[i], user_data);
        }
        epc_ast_builder_set_error(ctx, "Pointer expected no children, but got %u", count);
        return;
    }

    c_grammar_node_t *ast_node = create_terminal_node(AST_NODE_POINTER, node);
    if (ast_node == NULL)
    {
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_relational_expression(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_RELATIONAL_EXPRESSION);
}

static void
handle_equality_expression(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_EQUALITY_EXPRESSION);
}

static void
handle_and_expression(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_AND_EXPRESSION);
}

static void
handle_exclusive_or_expression(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_EXCLUSIVE_OR_EXPRESSION);
}

static void
handle_inclusive_or_expression(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_INCLUSIVE_OR_EXPRESSION);
}

static void
handle_logical_and_expression(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_LOGICAL_AND_EXPRESSION);
}

static void
handle_logical_or_expression(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_LOGICAL_OR_EXPRESSION);
}

static void
handle_function_call(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_FUNCTION_CALL);
}

static void
handle_array_index(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_ARRAY_SUBSCRIPT);
}

static void
handle_member_access_dot(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_MEMBER_ACCESS_DOT);
}

static void
handle_member_access_arrow(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_MEMBER_ACCESS_ARROW);
}

static void
handle_cast_expression(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_CAST_EXPRESSION);
}

static void
handle_init_declarator(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_INIT_DECLARATOR);
}

static void
handle_if_statement(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_IF_STATEMENT);
}

static void
handle_switch_statement(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_SWITCH_STATEMENT);
}

static void
handle_while_statement(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_WHILE_STATEMENT);
}

static void
handle_do_while_statement(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_DO_WHILE_STATEMENT);
}

static void
handle_for_statement(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_FOR_STATEMENT);
}

static void
handle_goto_statement(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_GOTO_STATEMENT);
}

static void
handle_continue_statement(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_CONTINUE_STATEMENT);
}

static void
handle_break_statement(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_BREAK_STATEMENT);
}

static void
handle_return_statement(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    handle_list_node(ctx, node, children, count, user_data, AST_NODE_RETURN_STATEMENT);
}

void c_grammar_ast_hook_registry_init(epc_ast_hook_registry_t *registry)
{
    epc_ast_hook_registry_set_free_node(registry, c_grammar_node_free);

    epc_ast_hook_registry_set_action(registry, AST_ACTION_IDENTIFIER, handle_identifier);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_INTEGER_LITERAL, handle_integer_literal);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_FUNCTION_CALL, handle_function_call);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ARRAY_INDEX, handle_array_index);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_MEMBER_ACCESS_DOT, handle_member_access_dot);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_MEMBER_ACCESS_ARROW, handle_member_access_arrow);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_OPERATOR, handle_operator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_UNARY_OP, handle_unary_op);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_CAST_EXPRESSION, handle_cast_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_BINARY_OP, handle_binary_op);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_RELATIONAL, handle_relational_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_EQUALITY, handle_equality_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_AND_EXPRESSION, handle_and_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_EXCLUSIVE_OR_EXPRESSION, handle_exclusive_or_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_INCLUSIVE_OR_EXPRESSION, handle_inclusive_or_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_LOGICAL_AND_EXPRESSION, handle_logical_and_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_LOGICAL_OR_EXPRESSION, handle_logical_or_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ASSIGNMENT, handle_assignment);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TYPE_SPECIFIER, handle_type_specifier);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DECL_SPECIFIERS, handle_decl_specifiers);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_POINTER, handle_pointer);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DIRECT_DECLARATOR, handle_direct_declarator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DECLARATOR_SUFFIX, handle_declarator_suffix);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DECLARATOR, handle_declarator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DECLARATION, handle_declaration);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_IF_STATEMENT, handle_if_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_SWITCH_STATEMENT, handle_switch_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_WHILE_STATEMENT, handle_while_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DO_WHILE_STATEMENT, handle_do_while_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_FOR_STATEMENT, handle_for_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_GOTO_STATEMENT, handle_goto_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_CONTINUE_STATEMENT, handle_continue_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_BREAK_STATEMENT, handle_break_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_RETURN_STATEMENT, handle_return_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_COMPOUND_STATEMENT, handle_compound_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_FUNCTION_DEFINITION, handle_function_definition);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TRANSLATION_UNIT, handle_translation_unit);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_INIT_DECLARATOR, handle_init_declarator);
}
