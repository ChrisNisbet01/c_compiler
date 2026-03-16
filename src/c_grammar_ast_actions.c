#include "c_grammar_ast_actions.h"
#include "c_grammar_ast.h"
#include "c_grammar_actions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
c_grammar_node_free(void * node_ptr, void * user_data)
{
    (void)user_data;
    c_grammar_node_t * node = (c_grammar_node_t *)node_ptr;
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
            free(node->data.terminal.text);
            break;
    }
    free(node);
}

static c_grammar_node_t *
create_list_node(c_grammar_node_type_t type, void ** children, int count)
{
    c_grammar_node_t * node = calloc(1, sizeof(*node));
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
    else
    {
        node->data.list.children = NULL;
    }
    return node;
}

static c_grammar_node_t *
create_terminal_node(c_grammar_node_type_t type, epc_cpt_node_t * node)
{
    c_grammar_node_t * ast_node = calloc(1, sizeof(*ast_node));
    if (ast_node == NULL)
    {
        return NULL;
    }
    ast_node->type = type;
    const char * text = epc_cpt_node_get_semantic_content(node);
    ast_node->data.terminal.text = strndup(text, epc_cpt_node_get_semantic_len(node));
    return ast_node;
}

/* --- Semantic Action Callbacks --- */

static void
handle_translation_unit(
    epc_ast_builder_ctx_t * ctx,
    epc_cpt_node_t * node,
    void ** children,
    int count,
    void * user_data)
{
    (void)node;
    c_grammar_node_t * ast_node = create_list_node(AST_NODE_TRANSLATION_UNIT, children, count);
    if (ast_node == NULL)
    {
        for (int i = 0; i < count; i++)
            c_grammar_node_free(children[i], user_data);
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_function_definition(
    epc_ast_builder_ctx_t * ctx,
    epc_cpt_node_t * node,
    void ** children,
    int count,
    void * user_data)
{
    (void)node;
    c_grammar_node_t * ast_node = create_list_node(AST_NODE_FUNCTION_DEFINITION, children, count);
    if (ast_node == NULL)
    {
        for (int i = 0; i < count; i++)
            c_grammar_node_free(children[i], user_data);
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_compound_statement(
    epc_ast_builder_ctx_t * ctx,
    epc_cpt_node_t * node,
    void ** children,
    int count,
    void * user_data)
{
    (void)node;
    c_grammar_node_t * ast_node = create_list_node(AST_NODE_COMPOUND_STATEMENT, children, count);
    if (ast_node == NULL)
    {
        for (int i = 0; i < count; i++)
            c_grammar_node_free(children[i], user_data);
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_declaration(
    epc_ast_builder_ctx_t * ctx,
    epc_cpt_node_t * node,
    void ** children,
    int count,
    void * user_data)
{
    (void)node;
    c_grammar_node_t * ast_node = create_list_node(AST_NODE_DECLARATION, children, count);
    if (ast_node == NULL)
    {
        for (int i = 0; i < count; i++)
            c_grammar_node_free(children[i], user_data);
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_integer_literal(
    epc_ast_builder_ctx_t * ctx,
    epc_cpt_node_t * node,
    void ** children,
    int count,
    void * user_data)
{
    (void)children;
    (void)count;
    (void)user_data;
    c_grammar_node_t * ast_node = create_terminal_node(AST_NODE_INTEGER_LITERAL, node);
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
    epc_ast_builder_ctx_t * ctx,
    epc_cpt_node_t * node,
    void ** children,
    int count,
    void * user_data)
{
    (void)children;
    (void)count;
    (void)user_data;
    c_grammar_node_t * ast_node = create_terminal_node(AST_NODE_IDENTIFIER, node);
    if (ast_node == NULL)
    {
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_decl_specifiers(
    epc_ast_builder_ctx_t * ctx,
    epc_cpt_node_t * node,
    void ** children,
    int count,
    void * user_data)
{
    (void)node;
    c_grammar_node_t * ast_node = create_list_node(AST_NODE_DECL_SPECIFIERS, children, count);
    if (ast_node == NULL)
    {
        for (int i = 0; i < count; i++)
            c_grammar_node_free(children[i], user_data);
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_assignment(
    epc_ast_builder_ctx_t * ctx,
    epc_cpt_node_t * node,
    void ** children,
    int count,
    void * user_data)
{
    (void)node;
    c_grammar_node_t * ast_node = create_list_node(AST_NODE_ASSIGNMENT, children, count);
    if (ast_node == NULL)
    {
        for (int i = 0; i < count; i++)
            c_grammar_node_free(children[i], user_data);
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_type_specifier(
    epc_ast_builder_ctx_t * ctx,
    epc_cpt_node_t * node,
    void ** children,
    int count,
    void * user_data)
{
    (void)children;
    (void)count;
    (void)user_data;
    c_grammar_node_t * ast_node = create_terminal_node(AST_NODE_TYPE_SPECIFIER, node);
    if (ast_node == NULL)
    {
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_binary_op(
    epc_ast_builder_ctx_t * ctx,
    epc_cpt_node_t * node,
    void ** children,
    int count,
    void * user_data)
{
    (void)node;
    c_grammar_node_t * ast_node = create_list_node(AST_NODE_BINARY_OP, children, count);
    if (ast_node == NULL)
    {
        for (int i = 0; i < count; i++)
            c_grammar_node_free(children[i], user_data);
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_unary_op(
    epc_ast_builder_ctx_t * ctx,
    epc_cpt_node_t * node,
    void ** children,
    int count,
    void * user_data)
{
    (void)node;
    c_grammar_node_t * ast_node = create_list_node(AST_NODE_UNARY_OP, children, count);
    if (ast_node == NULL)
    {
        for (int i = 0; i < count; i++)
            c_grammar_node_free(children[i], user_data);
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_operator(
    epc_ast_builder_ctx_t * ctx,
    epc_cpt_node_t * node,
    void ** children,
    int count,
    void * user_data)
{
    (void)children;
    (void)count;
    (void)user_data;
    c_grammar_node_t * ast_node = create_terminal_node(AST_NODE_OPERATOR, node);
    if (ast_node == NULL)
    {
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

void
c_grammar_ast_hook_registry_init(epc_ast_hook_registry_t * registry)
{
    epc_ast_hook_registry_set_free_node(registry, c_grammar_node_free);

    epc_ast_hook_registry_set_action(registry, AST_ACTION_TRANSLATION_UNIT, handle_translation_unit);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_FUNCTION_DEFINITION, handle_function_definition);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_COMPOUND_STATEMENT, handle_compound_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DECLARATION, handle_declaration);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_INTEGER_LITERAL, handle_integer_literal);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_IDENTIFIER, handle_identifier);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DECL_SPECIFIERS, handle_decl_specifiers);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ASSIGNMENT, handle_assignment);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TYPE_SPECIFIER, handle_type_specifier);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_BINARY_OP, handle_binary_op);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_UNARY_OP, handle_unary_op);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_OPERATOR, handle_operator);
}
