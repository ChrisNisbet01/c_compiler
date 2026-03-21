#include "c_grammar_ast_actions.h"

#include "c_grammar_actions.h"
#include "c_grammar_ast.h"

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
    if (node->is_terminal_node)
    {
        free(node->data.terminal.text);
    }
    else if (node->data.list.children != NULL)
    {
        for (size_t i = 0; i < node->data.list.count; i++)
        {
            c_grammar_node_free(node->data.list.children[i], user_data);
        }
        free(node->data.list.children);
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

    return node;
}

static c_grammar_node_t *
create_empty_terminal_node(c_grammar_node_type_t type)
{
    c_grammar_node_t * ast_node = calloc(1, sizeof(*ast_node));
    if (ast_node == NULL)
    {
        return NULL;
    }
    ast_node->type = type;
    ast_node->is_terminal_node = true;
    return ast_node;
}

static c_grammar_node_t *
create_terminal_node(c_grammar_node_type_t type, epc_cpt_node_t * node)
{
    c_grammar_node_t * ast_node = create_empty_terminal_node(type);
    if (ast_node == NULL)
    {
        return NULL;
    }
    char const * text = epc_cpt_node_get_semantic_content(node);
    ast_node->data.terminal.text = strndup(text, epc_cpt_node_get_semantic_len(node));
    return ast_node;
}

/* --- Semantic Action Callbacks --- */

static c_grammar_node_t *
handle_list_node(
    epc_ast_builder_ctx_t * ctx,
    epc_cpt_node_t * node,
    void ** children,
    int count,
    void * user_data,
    c_grammar_node_type_t type
)
{
    (void)node;

    c_grammar_node_t * ast_node = create_list_node(type, children, count);
    if (ast_node == NULL)
    {
        for (int i = 0; i < count; i++)
        {
            c_grammar_node_free(children[i], user_data);
        }
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
    }
    return ast_node;
}

static void
handle_translation_unit(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TRANSLATION_UNIT);
    epc_ast_push(ctx, ast_node);
}

static void
handle_function_definition(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_FUNCTION_DEFINITION);
    epc_ast_push(ctx, ast_node);
}

static void
handle_compound_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_COMPOUND_STATEMENT);
    epc_ast_push(ctx, ast_node);
}

static void
handle_declaration(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DECLARATION);
    epc_ast_push(ctx, ast_node);
}

static void
handle_integer_base(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count > 0)
    {
        for (int i = 0; i < count; i++)
        {
            c_grammar_node_free(children[i], user_data);
        }
        epc_ast_builder_set_error(ctx, "Integer base expected no children, but got %u", count);
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(AST_NODE_INTEGER_BASE, node);
    if (ast_node == NULL)
    {
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_float_base(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count > 0)
    {
        for (int i = 0; i < count; i++)
        {
            c_grammar_node_free(children[i], user_data);
        }
        epc_ast_builder_set_error(ctx, "Float base expected no children, but got %u", count);
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(AST_NODE_FLOAT_BASE, node);
    if (ast_node == NULL)
    {
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_integer_value(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    /* Note that the suffix is optional, so there should be either 1 or 2 child nodes. */
    if (count == 0 || count > 2)
    {
        for (int i = 0; i < count; i++)
        {
            c_grammar_node_free(children[i], user_data);
        }
        epc_ast_builder_set_error(ctx, "Integer value expected 1 or 2 children, but got %u", count);
        return;
    }
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_INTEGER_VALUE);
    epc_ast_push(ctx, ast_node);
}

static void
handle_float_value(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    /* Note that the suffix is optional, so there should be either 1 or 2 child nodes. */
    if (count == 0 || count > 2)
    {
        for (int i = 0; i < count; i++)
        {
            c_grammar_node_free(children[i], user_data);
        }
        epc_ast_builder_set_error(ctx, "Float value expected 1 or 2 children, but got %u", count);
        return;
    }
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_FLOAT_VALUE);
    epc_ast_push(ctx, ast_node);
}

static void
handle_string_literal(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count > 0)
    {
        for (int i = 0; i < count; i++)
        {
            c_grammar_node_free(children[i], user_data);
        }
        epc_ast_builder_set_error(ctx, "String literal expected no children, but got %u", count);
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(AST_NODE_STRING_LITERAL, node);
    if (ast_node == NULL)
    {
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_character_literal(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count > 0)
    {
        for (int i = 0; i < count; i++)
        {
            c_grammar_node_free(children[i], user_data);
        }
        epc_ast_builder_set_error(ctx, "Character literal expected no children, but got %u", count);
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(AST_NODE_CHARACTER_LITERAL, node);
    if (ast_node == NULL)
    {
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_literal_suffix(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count > 0)
    {
        for (int i = 0; i < count; i++)
        {
            c_grammar_node_free(children[i], user_data);
        }
        epc_ast_builder_set_error(ctx, "Literal suffix expected no children, but got %u", count);
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(AST_NODE_LITERAL_SUFFIX, node);
    if (ast_node == NULL)
    {
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_identifier(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
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
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DECL_SPECIFIERS);
    epc_ast_push(ctx, ast_node);
}

static void
handle_assignment(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_ASSIGNMENT);
    epc_ast_push(ctx, ast_node);
}

static void
handle_type_specifier(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    // Type specifier can have children (e.g., for struct types)
    // When count == 0, the node itself is a terminal (like KwFloat)
    c_grammar_node_t * ast_node;
    if (count == 0)
    {
        ast_node = create_terminal_node(AST_NODE_TYPE_SPECIFIER, node);
    }
    else
    {
        ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TYPE_SPECIFIER);
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_unary_op(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_UNARY_OP);
    epc_ast_push(ctx, ast_node);
}

static void
handle_operator(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
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

    c_grammar_node_t * ast_node = create_terminal_node(AST_NODE_OPERATOR, node);
    if (ast_node == NULL)
    {
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_declarator(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DECLARATOR);
    epc_ast_push(ctx, ast_node);
}

static void
handle_direct_declarator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DIRECT_DECLARATOR);
    epc_ast_push(ctx, ast_node);
}

static void
handle_declarator_suffix(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DECLARATOR_SUFFIX);
    epc_ast_push(ctx, ast_node);
}

static void
handle_pointer(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
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

    c_grammar_node_t * ast_node = create_terminal_node(AST_NODE_POINTER, node);
    if (ast_node == NULL)
    {
        epc_ast_builder_set_error(ctx, "Memory allocation failed");
        return;
    }
    epc_ast_push(ctx, ast_node);
}

static void
handle_relational_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_RELATIONAL_EXPRESSION);
    epc_ast_push(ctx, ast_node);
}

static void
handle_equality_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_EQUALITY_EXPRESSION);
    epc_ast_push(ctx, ast_node);
}

static void
handle_bitwise_and_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_BITWISE_EXPRESSION);
    ast_node->bitwise_op.op = BITWISE_OP_AND;

    epc_ast_push(ctx, ast_node);
}

static void
handle_bitwise_exclusive_or_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_BITWISE_EXPRESSION);
    ast_node->bitwise_op.op = BITWISE_OP_XOR;

    epc_ast_push(ctx, ast_node);
}

static void
handle_bitwise_inclusive_or_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_BITWISE_EXPRESSION);
    ast_node->bitwise_op.op = BITWISE_OP_OR;

    epc_ast_push(ctx, ast_node);
}

static void
handle_logical_and_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_LOGICAL_AND_EXPRESSION);
    epc_ast_push(ctx, ast_node);
}

static void
handle_logical_or_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_LOGICAL_OR_EXPRESSION);
    epc_ast_push(ctx, ast_node);
}

static void
handle_shift_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 3)
    {
        for (int i = 0; i < count; i++)
        {
            c_grammar_node_free(children[i], user_data);
        }
        epc_ast_builder_set_error(ctx, "ShiftExpression expected exactly 3 children, but got %d", count);
        return;
    }

    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_SHIFT_EXPRESSION);

    if (ast_node)
    {
        c_grammar_node_t * op_node = ast_node->data.list.children[1];
        if (op_node && op_node->is_terminal_node && op_node->data.terminal.text)
        {
            if (strcmp(op_node->data.terminal.text, "<<") == 0)
            {
                ast_node->shift_op.op = SHIFT_OP_LL;
            }
            else if (strcmp(op_node->data.terminal.text, ">>") == 0)
            {
                ast_node->shift_op.op = SHIFT_OP_AR;
            }
        }
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_arithmetic_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 3)
    {
        for (int i = 0; i < count; i++)
        {
            c_grammar_node_free(children[i], user_data);
        }
        epc_ast_builder_set_error(ctx, "ArithmeticExpression expected exactly 3 children, but got %d", count);
        return;
    }

    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_ARITHMETIC_EXPRESSION);

    if (ast_node)
    {
        c_grammar_node_t * op_node = ast_node->data.list.children[1];
        if (op_node && op_node->is_terminal_node && op_node->data.terminal.text)
        {
            if (strcmp(op_node->data.terminal.text, "+") == 0)
            {
                ast_node->arith_op.op = ARITH_OP_ADD;
            }
            else if (strcmp(op_node->data.terminal.text, "-") == 0)
            {
                ast_node->arith_op.op = ARITH_OP_SUB;
            }
            else if (strcmp(op_node->data.terminal.text, "*") == 0)
            {
                ast_node->arith_op.op = ARITH_OP_MUL;
            }
            else if (strcmp(op_node->data.terminal.text, "/") == 0)
            {
                ast_node->arith_op.op = ARITH_OP_DIV;
            }
            else if (strcmp(op_node->data.terminal.text, "%") == 0)
            {
                ast_node->arith_op.op = ARITH_OP_MOD;
            }
        }
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_function_call(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_FUNCTION_CALL);
    epc_ast_push(ctx, ast_node);
}

static void
handle_postfix_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count == 1)
    {
        epc_ast_push(ctx, children[0]);
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_POSTFIX_EXPRESSION);
    epc_ast_push(ctx, ast_node);
}

static void
handle_array_index(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_ARRAY_SUBSCRIPT);
    epc_ast_push(ctx, ast_node);
}

static void
handle_member_access_dot(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_MEMBER_ACCESS_DOT);
    epc_ast_push(ctx, ast_node);
}

static void
handle_member_access_arrow(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_MEMBER_ACCESS_ARROW);
    epc_ast_push(ctx, ast_node);
}

static void
handle_cast_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_CAST_EXPRESSION);
    epc_ast_push(ctx, ast_node);
}

static void
handle_init_declarator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_INIT_DECLARATOR);
    epc_ast_push(ctx, ast_node);
}

static void
handle_initializer_list(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_INITIALIZER_LIST);
    epc_ast_push(ctx, ast_node);
}

static void
handle_if_statement(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_IF_STATEMENT);
    epc_ast_push(ctx, ast_node);
}

static void
handle_switch_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_SWITCH_STATEMENT);
    epc_ast_push(ctx, ast_node);
}

static void
handle_while_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_WHILE_STATEMENT);
    epc_ast_push(ctx, ast_node);
}

static void
handle_do_while_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DO_WHILE_STATEMENT);
    epc_ast_push(ctx, ast_node);
}

static void
handle_for_statement(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_FOR_STATEMENT);
    epc_ast_push(ctx, ast_node);
}

static void
handle_labeled_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_LABELED_STATEMENT);
    epc_ast_push(ctx, ast_node);
}

static void
handle_labeled_identifier(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_LABELED_IDENTIFIER);
    epc_ast_push(ctx, ast_node);
}

static void
handle_case_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_CASE_STATEMENT);
    epc_ast_push(ctx, ast_node);
}

static void
handle_default_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DEFAULT_STATEMENT);
    epc_ast_push(ctx, ast_node);
}

static void
handle_goto_statement(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_GOTO_STATEMENT);
    epc_ast_push(ctx, ast_node);
}

static void
handle_continue_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_CONTINUE_STATEMENT);
    epc_ast_push(ctx, ast_node);
}

static void
handle_break_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_BREAK_STATEMENT);
    epc_ast_push(ctx, ast_node);
}

static void
handle_return_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_RETURN_STATEMENT);
    epc_ast_push(ctx, ast_node);
}

static void
handle_type_name(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TYPE_NAME);
    epc_ast_push(ctx, ast_node);
}

static void
handle_expression_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_EXPRESSION_STATEMENT);
    epc_ast_push(ctx, ast_node);
}

static void
handle_struct_definition(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_STRUCT_DEFINITION);
    epc_ast_push(ctx, ast_node);
}

void
c_grammar_ast_hook_registry_init(epc_ast_hook_registry_t * registry)
{
    epc_ast_hook_registry_set_free_node(registry, c_grammar_node_free);

    epc_ast_hook_registry_set_action(registry, AST_ACTION_IDENTIFIER, handle_identifier);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_INTEGER_BASE, handle_integer_base);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_FLOAT_BASE, handle_float_base);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_INTEGER_VALUE, handle_integer_value);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_FLOAT_VALUE, handle_float_value);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_STRING_LITERAL, handle_string_literal);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_CHARACTER_LITERAL, handle_character_literal);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_LITERAL_SUFFIX, handle_literal_suffix);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_FUNCTION_CALL, handle_function_call);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_POSTFIX_EXPRESSION, handle_postfix_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ARRAY_SUBSCRIPT, handle_array_index);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_MEMBER_ACCESS_DOT, handle_member_access_dot);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_MEMBER_ACCESS_ARROW, handle_member_access_arrow);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_OPERATOR, handle_operator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_UNARY_OP, handle_unary_op);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_CAST_EXPRESSION, handle_cast_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_RELATIONAL, handle_relational_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_EQUALITY, handle_equality_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_BITWISE_AND_EXPRESSION, handle_bitwise_and_expression);
    epc_ast_hook_registry_set_action(
        registry, AST_ACTION_BITWISE_EXCLUSIVE_OR_EXPRESSION, handle_bitwise_exclusive_or_expression
    );
    epc_ast_hook_registry_set_action(
        registry, AST_ACTION_BITWISE_INCLUSIVE_OR_EXPRESSION, handle_bitwise_inclusive_or_expression
    );
    epc_ast_hook_registry_set_action(registry, AST_ACTION_LOGICAL_AND_EXPRESSION, handle_logical_and_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_LOGICAL_OR_EXPRESSION, handle_logical_or_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_SHIFT_EXPRESSION, handle_shift_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ARITHMETIC_EXPRESSION, handle_arithmetic_expression);
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
    epc_ast_hook_registry_set_action(registry, AST_ACTION_LABELED_STATEMENT, handle_labeled_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_LABELED_IDENTIFIER, handle_labeled_identifier);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_CASE_STATEMENT, handle_case_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DEFAULT_STATEMENT, handle_default_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_GOTO_STATEMENT, handle_goto_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_CONTINUE_STATEMENT, handle_continue_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_BREAK_STATEMENT, handle_break_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_RETURN_STATEMENT, handle_return_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_COMPOUND_STATEMENT, handle_compound_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_FUNCTION_DEFINITION, handle_function_definition);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TRANSLATION_UNIT, handle_translation_unit);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_INIT_DECLARATOR, handle_init_declarator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_INITIALIZER_LIST, handle_initializer_list);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TYPE_NAME, handle_type_name);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_EXPRESSION_STATEMENT, handle_expression_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_STRUCT_SPECIFIER, handle_struct_definition);
}
