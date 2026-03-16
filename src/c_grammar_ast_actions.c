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
        node->data.list.children = calloc(count, sizeof(*node->data.list.children));
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

/* --- Semantic Action Callbacks --- */

static void
handle_translation_unit(
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    (void)node;
    c_grammar_node_t *ast_node = create_list_node(AST_NODE_TRANSLATION_UNIT, children, count);
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
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    (void)node;
    c_grammar_node_t *ast_node = create_list_node(AST_NODE_FUNCTION_DEFINITION, children, count);
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
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    (void)node;
    c_grammar_node_t *ast_node = create_list_node(AST_NODE_COMPOUND_STATEMENT, children, count);
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
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    (void)node;
    c_grammar_node_t *ast_node = create_list_node(AST_NODE_DECLARATION, children, count);
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
    epc_ast_builder_ctx_t *ctx,
    epc_cpt_node_t *node,
    void **children,
    int count,
    void *user_data)
{
    (void)children;
    (void)count;
    (void)user_data;
    c_grammar_node_t *ast_node = calloc(1, sizeof(c_grammar_node_t));
    if (ast_node == NULL)
    {
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    ast_node->type = AST_NODE_INTEGER_LITERAL;
    const char *text = epc_cpt_node_get_semantic_content(node);
    ast_node->data.terminal.text = strndup(text, epc_cpt_node_get_semantic_len(node));
    ast_node->data.terminal.value = strtol(ast_node->data.terminal.text, NULL, 0);
    epc_ast_push(ctx, ast_node);
}

void c_grammar_ast_hook_registry_init(epc_ast_hook_registry_t *registry)
{
    epc_ast_hook_registry_set_free_node(registry, c_grammar_node_free);

    epc_ast_hook_registry_set_action(registry, AST_ACTION_TRANSLATION_UNIT, handle_translation_unit);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_FUNCTION_DEFINITION, handle_function_definition);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_COMPOUND_STATEMENT, handle_compound_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DECLARATION, handle_declaration);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_INTEGER_LITERAL, handle_integer_literal);
}
