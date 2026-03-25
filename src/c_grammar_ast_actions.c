#include "c_grammar_ast_actions.h"

#include "ast_node_name.h"
#include "c_grammar_actions.h"
#include "c_grammar_ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
free_ast_node_children(void ** children, int count, void * user_data)
{
    if (children == NULL)
    {
        return;
    }
    for (int i = 0; i < count; i++)
    {
        c_grammar_node_free(children[i], user_data);
    }
}

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
    else
    {
        free_ast_node_children((void **)node->data.list.children, node->data.list.count, user_data);
        free(node->data.list.children);
    }
    free((char *)node->op.text);
    c_grammar_node_free((c_grammar_node_t *)node->lhs, user_data);
    c_grammar_node_free((c_grammar_node_t *)node->rhs, user_data);
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
create_terminal_node(epc_ast_builder_ctx_t * ctx, c_grammar_node_type_t type, epc_cpt_node_t * node)
{
    c_grammar_node_t * ast_node = create_empty_terminal_node(type);
    if (ast_node == NULL)
    {
        epc_ast_builder_set_error(ctx, "%s: Memory allocation failed", get_node_type_name_from_type(type));
        return NULL;
    }

    char const * text = epc_cpt_node_get_semantic_content(node);
    ast_node->data.terminal.text = strndup(text, epc_cpt_node_get_semantic_len(node));
    if (ast_node->data.terminal.text == NULL)
    {
        free(ast_node);
        ast_node = NULL;
        epc_ast_builder_set_error(ctx, "%s: Memory allocation failed", get_node_type_name_from_type(type));
    }

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
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "%s: Memory allocation failed", get_node_type_name_from_type(type));
    }
    return ast_node;
}

static void
handle_translation_unit(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TRANSLATION_UNIT);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_function_definition(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_FUNCTION_DEFINITION);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_compound_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_COMPOUND_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_optional_kw_extension(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_OPTIONAL_KW_EXTENSION);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_optional_init_declarator_list(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_OPTIONAL_INIT_DECLARATOR_LIST);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_declaration(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    /* [ OptionalKwExtension DeclarationSpecifiers OptionalInitDeclaratorList ] */
    if (count != 3)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 3 children, but got %d", get_node_type_name_from_type(AST_NODE_DECLARATION), count
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DECLARATION);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_integer_base(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count > 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected no children, but got %u", get_node_type_name_from_type(AST_NODE_INTEGER_BASE), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_INTEGER_BASE, node);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_float_base(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count > 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected no children, but got %u", get_node_type_name_from_type(AST_NODE_FLOAT_BASE), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_FLOAT_BASE, node);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_integer_literal(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    /* Note that the suffix is optional, so there should be either 1 or 2 child nodes. */
    if (count == 0 || count > 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected 1 or 2 children, but got %u",
            get_node_type_name_from_type(AST_NODE_INTEGER_LITERAL),
            count
        );
        return;
    }
    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_INTEGER_LITERAL, node);
    if (ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }

    // Parse with base 0 to automatically handle 0x (hex) and 0 (octal)
    ast_node->integer_literal.value = strtoull(ast_node->data.terminal.text, NULL, 0);

    if (count == 2)
    {
        c_grammar_node_t * suffix_node = children[1];
        char * suffix_text = suffix_node->is_terminal_node ? suffix_node->data.terminal.text : NULL;

        if (suffix_text != NULL)
        {
            if (strchr(suffix_text, 'u') || strchr(suffix_text, 'U'))
            {
                ast_node->integer_literal.is_unsigned = true;
            }
            if (strchr(suffix_text, 'l') || strchr(suffix_text, 'L'))
            {
                ast_node->integer_literal.is_long = true;
            }
        }
    }

    free_ast_node_children(children, count, user_data);

    epc_ast_push(ctx, ast_node);
}

static void
handle_float_literal(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    /* Note that the suffix is optional, so there should be either 1 or 2 child nodes. */
    if (count == 0 || count > 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 1 or 2 children, but got %u", get_node_type_name_from_type(AST_NODE_FLOAT_LITERAL), count
        );
        return;
    }
    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_FLOAT_LITERAL, node);
    if (ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }

    c_grammar_node_t * suffix_node = count == 2 ? children[1] : NULL;
    char * full_text = ast_node->data.terminal.text;

    ast_node->float_literal.value = strtold(full_text, NULL);
    ast_node->float_literal.type = FLOAT_LITERAL_TYPE_DOUBLE; /* Default to double. */
    if (suffix_node != NULL)
    {
        char * suffix_text = suffix_node->is_terminal_node ? suffix_node->data.terminal.text : "";

        if (strchr(suffix_text, 'f') || strchr(suffix_text, 'F'))
        {
            ast_node->float_literal.type = FLOAT_LITERAL_TYPE_FLOAT;
        }
        else if (strchr(suffix_text, 'l') || strchr(suffix_text, 'L'))
        {
            ast_node->float_literal.type = FLOAT_LITERAL_TYPE_LONG_DOUBLE;
        }
    }

    free_ast_node_children(children, count, user_data);

    epc_ast_push(ctx, ast_node);
}

static void
handle_string_literal(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count > 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected no children, but got %u", get_node_type_name_from_type(AST_NODE_STRING_LITERAL), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_STRING_LITERAL, node);
    if (ast_node == NULL)
    {
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
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected no children, but got %u", get_node_type_name_from_type(AST_NODE_CHARACTER_LITERAL), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_CHARACTER_LITERAL, node);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_literal_suffix(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count > 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected no children, but got %u", get_node_type_name_from_type(AST_NODE_LITERAL_SUFFIX), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_LITERAL_SUFFIX, node);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_identifier(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count > 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected no children, but got %u", get_node_type_name_from_type(AST_NODE_IDENTIFIER), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_IDENTIFIER, node);
    if (ast_node == NULL)
    {
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
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_assignment_operator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count > 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected no children, but got %u",
            get_node_type_name_from_type(AST_NODE_ASSIGNMENT_OPERATOR),
            count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_ASSIGNMENT_OPERATOR, node);
    if (ast_node == NULL)
    {
        return;
    }

    char const * text = ast_node->data.terminal.text;

    if (strcmp(text, "=") == 0)
        ast_node->op.assign.op = ASSIGN_OP_SIMPLE;
    else if (strcmp(text, "<<=") == 0)
        ast_node->op.assign.op = ASSIGN_OP_SHL;
    else if (strcmp(text, ">>=") == 0)
        ast_node->op.assign.op = ASSIGN_OP_SHR;
    else if (strcmp(text, "+=") == 0)
        ast_node->op.assign.op = ASSIGN_OP_ADD;
    else if (strcmp(text, "-=") == 0)
        ast_node->op.assign.op = ASSIGN_OP_SUB;
    else if (strcmp(text, "*=") == 0)
        ast_node->op.assign.op = ASSIGN_OP_MUL;
    else if (strcmp(text, "/=") == 0)
        ast_node->op.assign.op = ASSIGN_OP_DIV;
    else if (strcmp(text, "%=") == 0)
        ast_node->op.assign.op = ASSIGN_OP_MOD;
    else if (strcmp(text, "&=") == 0)
        ast_node->op.assign.op = ASSIGN_OP_AND;
    else if (strcmp(text, "^=") == 0)
        ast_node->op.assign.op = ASSIGN_OP_XOR;
    else if (strcmp(text, "|=") == 0)
        ast_node->op.assign.op = ASSIGN_OP_OR;

    epc_ast_push(ctx, ast_node);
}

static void
handle_assignment(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    (void)node;

    if (count != 3)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 3 children, but got %d", get_node_type_name_from_type(AST_NODE_ASSIGNMENT), count
        );
        return;
    }

    c_grammar_node_t * op_node = children[1];
    if (op_node->type != AST_NODE_ASSIGNMENT_OPERATOR)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected shift operator node at index 1, but got %s",
            get_node_type_name_from_type(AST_NODE_ASSIGNMENT),
            get_node_type_name_from_node(op_node)
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_ASSIGNMENT, node);
    if (ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }

    ast_node->lhs = children[0];
    ast_node->rhs = children[2];
    ast_node->op.assign.op = op_node->op.assign.op;
    ast_node->op.text = op_node->data.terminal.text;
    op_node->data.terminal.text = NULL;
    c_grammar_node_free(op_node, user_data);

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
        ast_node = create_terminal_node(ctx, AST_NODE_TYPE_SPECIFIER, node);
    }
    else
    {
        ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TYPE_SPECIFIER);
    }
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_unary_operator(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count > 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected no children, but got %u", get_node_type_name_from_type(AST_NODE_UNARY_OPERATOR), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_UNARY_OPERATOR, node);
    if (ast_node == NULL)
    {
        return;
    }

    char const * op_text = ast_node->data.terminal.text;

    if (strcmp(op_text, "+") == 0)
        ast_node->op.unary.op = UNARY_OP_PLUS;
    else if (strcmp(op_text, "-") == 0)
        ast_node->op.unary.op = UNARY_OP_MINUS;
    else if (strcmp(op_text, "!") == 0)
        ast_node->op.unary.op = UNARY_OP_NOT;
    else if (strcmp(op_text, "~") == 0)
        ast_node->op.unary.op = UNARY_OP_BITNOT;
    else if (strcmp(op_text, "&") == 0)
        ast_node->op.unary.op = UNARY_OP_ADDR;
    else if (strcmp(op_text, "*") == 0)
        ast_node->op.unary.op = UNARY_OP_DEREF;
    else if (strcmp(op_text, "++") == 0)
        ast_node->op.unary.op = UNARY_OP_INC;
    else if (strcmp(op_text, "--") == 0)
        ast_node->op.unary.op = UNARY_OP_DEC;
    else if (strcmp(op_text, "sizeof") == 0)
        ast_node->op.unary.op = UNARY_OP_SIZEOF;
    else if (strcmp(op_text, "__alignof__") == 0 || strcmp(op_text, "_Alignof") == 0)
        ast_node->op.unary.op = UNARY_OP_ALIGNOF;
    else
    {
        epc_ast_builder_set_error(
            ctx, "%s: Unknown operator: %s", get_node_type_name_from_type(AST_NODE_UNARY_OPERATOR), op_text
        );
        c_grammar_node_free(ast_node, user_data);
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_unary_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected at 2 children, but got %u", get_node_type_name_from_type(AST_NODE_UNARY_EXPRESSION), count
        );
        return;
    }

    /* The first child should always be the unary operator node. */
    c_grammar_node_t * op_node = (c_grammar_node_t *)children[0];
    if (op_node->type != AST_NODE_UNARY_OPERATOR)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected operator node, but got %s",
            get_node_type_name_from_type(AST_NODE_UNARY_EXPRESSION),
            get_node_type_name_from_node(op_node)
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_UNARY_EXPRESSION, node);
    if (ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }

    /* The operand should be children[1]. Let's save it into node->lhs. */
    ast_node->lhs = children[1];
    ast_node->op.unary.op = op_node->op.unary.op;
    ast_node->op.text = op_node->data.terminal.text;
    op_node->data.terminal.text = NULL;
    c_grammar_node_free(op_node, user_data);

    epc_ast_push(ctx, ast_node);
}

static void
handle_declarator(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DECLARATOR);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_direct_declarator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DIRECT_DECLARATOR);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_declarator_suffix(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DECLARATOR_SUFFIX);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_pointer(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count > 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected no children, but got %u", get_node_type_name_from_type(AST_NODE_POINTER), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_POINTER, node);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_relational_operator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count > 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected no children, but got %u",
            get_node_type_name_from_type(AST_NODE_RELATIONAL_OPERATOR),
            count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_RELATIONAL_OPERATOR, node);
    if (ast_node == NULL)
    {
        return;
    }

    char const * op_text = ast_node->data.terminal.text;

    if (strcmp(op_text, "<") == 0)
    {
        ast_node->op.rel.op = REL_OP_LT;
    }
    else if (strcmp(op_text, ">") == 0)
    {
        ast_node->op.rel.op = REL_OP_GT;
    }
    else if (strcmp(op_text, "<=") == 0)
    {
        ast_node->op.rel.op = REL_OP_LE;
    }
    else if (strcmp(op_text, ">=") == 0)
    {
        ast_node->op.rel.op = REL_OP_GE;
    }
    else
    {
        epc_ast_builder_set_error(
            ctx, "%s: Unknown operator: %s", get_node_type_name_from_type(AST_NODE_RELATIONAL_OPERATOR), op_text
        );
        c_grammar_node_free(ast_node, user_data);
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_relational_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 3)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected 3 children, but got %d",
            get_node_type_name_from_type(AST_NODE_RELATIONAL_EXPRESSION),
            count
        );
        return;
    }

    c_grammar_node_t * op_node = children[1];
    if (op_node->type != AST_NODE_RELATIONAL_OPERATOR)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected relational operator node at index 1, but got %s",
            get_node_type_name_from_type(AST_NODE_RELATIONAL_EXPRESSION),
            get_node_type_name_from_node(op_node)
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_RELATIONAL_EXPRESSION, node);
    if (ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }

    ast_node->lhs = children[0];
    ast_node->rhs = children[2];
    ast_node->op.rel.op = op_node->op.rel.op;
    ast_node->op.text = op_node->data.terminal.text;
    op_node->data.terminal.text = NULL;
    c_grammar_node_free(op_node, user_data);

    epc_ast_push(ctx, ast_node);
}

static void
handle_equality_operator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count > 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected no children, but got %u", get_node_type_name_from_type(AST_NODE_EQUALITY_OPERATOR), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_EQUALITY_OPERATOR, node);
    if (ast_node == NULL)
    {
        return;
    }

    char const * op_text = ast_node->data.terminal.text;

    if (strcmp(op_text, "==") == 0)
    {
        ast_node->op.eq.op = EQ_OP_EQ;
    }
    else if (strcmp(op_text, "!=") == 0)
    {
        ast_node->op.eq.op = EQ_OP_NE;
    }
    else
    {
        epc_ast_builder_set_error(
            ctx, "%s, Unknown operator: %s", get_node_type_name_from_type(AST_NODE_EQUALITY_OPERATOR), op_text
        );
        c_grammar_node_free(ast_node, user_data);
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_equality_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 3)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 3 children, but got %d", get_node_type_name_from_type(AST_NODE_EQUALITY_EXPRESSION), count
        );
        return;
    }

    c_grammar_node_t * op_node = children[1];
    if (op_node->type != AST_NODE_EQUALITY_OPERATOR)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected equality operator node at index 1, but got %s",
            get_node_type_name_from_type(AST_NODE_EQUALITY_EXPRESSION),
            get_node_type_name_from_node(op_node)
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_EQUALITY_EXPRESSION, node);
    if (ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }

    ast_node->lhs = children[0];
    ast_node->rhs = children[2];
    ast_node->op.eq.op = op_node->op.eq.op;
    ast_node->op.text = op_node->data.terminal.text;
    op_node->data.terminal.text = NULL;
    c_grammar_node_free(op_node, user_data);

    epc_ast_push(ctx, ast_node);
}

static void
handle_bitwise_and_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    (void)count;
    (void)user_data;

    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %d", get_node_type_name_from_type(AST_NODE_BITWISE_EXPRESSION), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_BITWISE_EXPRESSION, node);
    if (ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }

    ast_node->lhs = children[0];
    ast_node->rhs = children[1];
    ast_node->op.bitwise.op = BITWISE_OP_AND;
    ast_node->op.text = strdup("&");

    epc_ast_push(ctx, ast_node);
}

static void
handle_bitwise_exclusive_or_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %d", get_node_type_name_from_type(AST_NODE_BITWISE_EXPRESSION), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_BITWISE_EXPRESSION, node);
    if (ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }

    ast_node->lhs = children[0];
    ast_node->rhs = children[1];
    ast_node->op.bitwise.op = BITWISE_OP_XOR;
    ast_node->op.text = strdup("^");

    epc_ast_push(ctx, ast_node);
}

static void
handle_bitwise_inclusive_or_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %d", get_node_type_name_from_type(AST_NODE_BITWISE_EXPRESSION), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_BITWISE_EXPRESSION, node);
    if (ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }

    ast_node->lhs = children[0];
    ast_node->rhs = children[1];
    ast_node->op.bitwise.op = BITWISE_OP_OR;
    ast_node->op.text = strdup("|");

    epc_ast_push(ctx, ast_node);
}

static void
handle_logical_and_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %d", get_node_type_name_from_type(AST_NODE_LOGICAL_EXPRESSION), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_LOGICAL_EXPRESSION, node);
    if (ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }

    ast_node->lhs = children[0];
    ast_node->rhs = children[1];
    ast_node->op.logical.op = LOGICAL_OP_AND;
    ast_node->op.text = strdup("&&");

    epc_ast_push(ctx, ast_node);
}

static void
handle_logical_or_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %d", get_node_type_name_from_type(AST_NODE_LOGICAL_EXPRESSION), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_LOGICAL_EXPRESSION, node);
    if (ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }

    ast_node->lhs = children[0];
    ast_node->rhs = children[1];
    ast_node->op.logical.op = LOGICAL_OP_OR;
    ast_node->op.text = strdup("||");

    epc_ast_push(ctx, ast_node);
}

static void
handle_shift_operator(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count > 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected no children, but got %u", get_node_type_name_from_type(AST_NODE_SHIFT_OPERATOR), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_SHIFT_OPERATOR, node);
    if (ast_node == NULL)
    {
        return;
    }

    char const * op_text = ast_node->data.terminal.text;

    if (strcmp(op_text, "<<") == 0)
    {
        ast_node->op.shift.op = SHIFT_OP_LL;
    }
    else if (strcmp(op_text, ">>") == 0)
    {
        ast_node->op.shift.op = SHIFT_OP_AR;
    }
    else
    {
        epc_ast_builder_set_error(
            ctx, "%s: Unknown shift operator: %s", get_node_type_name_from_type(AST_NODE_SHIFT_OPERATOR), op_text
        );
        c_grammar_node_free(ast_node, user_data);
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_shift_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 3)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected exactly 3 children, but got %d",
            get_node_type_name_from_type(AST_NODE_SHIFT_EXPRESSION),
            count
        );
        return;
    }

    c_grammar_node_t * op_node = children[1];
    if (op_node->type != AST_NODE_SHIFT_OPERATOR)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected shift operator node at index 1, but got %s",
            get_node_type_name_from_type(AST_NODE_SHIFT_EXPRESSION),
            get_node_type_name_from_node(op_node)
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_SHIFT_EXPRESSION, node);
    if (ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }

    ast_node->lhs = children[0];
    ast_node->rhs = children[2];
    ast_node->op.shift.op = op_node->op.shift.op;
    ast_node->op.text = op_node->data.terminal.text;
    op_node->data.terminal.text = NULL;
    c_grammar_node_free(op_node, user_data);

    epc_ast_push(ctx, ast_node);
}

static void
handle_arithmetic_operator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count > 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected no children, but got %u",
            get_node_type_name_from_type(AST_NODE_ARITHMETIC_OPERATOR),
            count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_ARITHMETIC_OPERATOR, node);
    if (ast_node == NULL)
    {
        return;
    }

    char const * op_text = ast_node->data.terminal.text;
    if (strcmp(op_text, "+") == 0)
    {
        ast_node->op.arith.op = ARITH_OP_ADD;
    }
    else if (strcmp(op_text, "-") == 0)
    {
        ast_node->op.arith.op = ARITH_OP_SUB;
    }
    else if (strcmp(op_text, "*") == 0)
    {
        ast_node->op.arith.op = ARITH_OP_MUL;
    }
    else if (strcmp(op_text, "/") == 0)
    {
        ast_node->op.arith.op = ARITH_OP_DIV;
    }
    else if (strcmp(op_text, "%") == 0)
    {
        ast_node->op.arith.op = ARITH_OP_MOD;
    }
    else
    {
        epc_ast_builder_set_error(
            ctx,
            "%s: Unknown arithmetic operator: %s",
            get_node_type_name_from_type(AST_NODE_ARITHMETIC_OPERATOR),
            op_text
        );
        c_grammar_node_free(ast_node, user_data);
        return;
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
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected 3 children, but got %d",
            get_node_type_name_from_type(AST_NODE_ARITHMETIC_EXPRESSION),
            count
        );
        return;
    }

    c_grammar_node_t * op_node = (c_grammar_node_t *)children[1];
    if (op_node->type != AST_NODE_ARITHMETIC_OPERATOR)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected arithmetic operator node at index 1, but got %s",
            get_node_type_name_from_type(AST_NODE_ARITHMETIC_EXPRESSION),
            get_node_type_name_from_node(op_node)
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_ARITHMETIC_EXPRESSION, node);
    if (ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }

    ast_node->lhs = children[0];
    ast_node->rhs = children[2];
    ast_node->op.arith.op = op_node->op.arith.op;
    ast_node->op.text = op_node->data.terminal.text;
    op_node->data.terminal.text = NULL;
    c_grammar_node_free(op_node, user_data);

    epc_ast_push(ctx, ast_node);
}

static void
handle_function_call(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_FUNCTION_CALL);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_postfix_operator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count > 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected no children, but got %u", get_node_type_name_from_type(AST_NODE_POSTFIX_OPERATOR), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_POSTFIX_OPERATOR, node);
    if (ast_node == NULL)
    {
        return;
    }

    char const * text = ast_node->data.terminal.text;

    if (strcmp(text, "++") == 0)
    {
        ast_node->op.postfix.op = POSTFIX_OP_INC;
    }
    else if (strcmp(text, "--") == 0)
    {
        ast_node->op.postfix.op = POSTFIX_OP_DEC;
    }
    else
    {
        epc_ast_builder_set_error(
            ctx, "%s: Unsupported postfix operator: %s", get_node_type_name_from_type(AST_NODE_POSTFIX_OPERATOR), text
        );
        c_grammar_node_free(ast_node, user_data);
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_postfix_parts(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_POSTFIX_PARTS);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_postfix_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 2) /* Expecting [PrimaryExpression][Postfix Parts placeholder] */
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %u", get_node_type_name_from_type(AST_NODE_POSTFIX_EXPRESSION), count
        );
        return;
    }

    c_grammar_node_t const * base = children[0];
    c_grammar_node_t const * postfix = children[1];

    if (postfix->type != AST_NODE_POSTFIX_PARTS)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected postfix parts node at index 1, but got %s",
            get_node_type_name_from_type(AST_NODE_POSTFIX_EXPRESSION),
            get_node_type_name_from_node(postfix)
        );
        return;
    }

    /*
       If the postfix parts list is empty then we just have a plain expression, so don't bother with a postfix
       expression node.
    */
    if (postfix->data.list.count == 0)
    {
        c_grammar_node_free((c_grammar_node_t *)postfix, user_data);
        epc_ast_push(ctx, (c_grammar_node_t *)base);
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_POSTFIX_EXPRESSION, node);
    if (ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }

    ast_node->lhs = base;
    ast_node->rhs = postfix;

    epc_ast_push(ctx, ast_node);
}

static void
handle_array_index(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_ARRAY_SUBSCRIPT);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_member_access_dot(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_MEMBER_ACCESS_DOT);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_member_access_arrow(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_MEMBER_ACCESS_ARROW);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_cast_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_CAST_EXPRESSION);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_init_declarator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_INIT_DECLARATOR);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_initializer_list(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_INITIALIZER_LIST);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_if_statement(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_IF_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_switch_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_SWITCH_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_while_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_WHILE_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_do_while_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DO_WHILE_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_for_statement(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_FOR_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_labeled_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_LABELED_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_labeled_identifier(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_LABELED_IDENTIFIER);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_case_label(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_CASE_LABEL);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_switch_case(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_SWITCH_CASE);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_default_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DEFAULT_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_goto_statement(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_GOTO_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_continue_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count > 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected no children, but got %u", get_node_type_name_from_type(AST_NODE_CONTINUE_STATEMENT), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_CONTINUE_STATEMENT, node);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_break_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count > 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected no children, but got %u", get_node_type_name_from_type(AST_NODE_BREAK_STATEMENT), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_BREAK_STATEMENT, node);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_return_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count > 1)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected no more than 1 child", get_node_type_name_from_type(AST_NODE_RETURN_STATEMENT)
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_RETURN_STATEMENT, node);
    if (ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }

    if (count == 1)
    {
        ast_node->lhs = children[0];
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_type_name(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TYPE_NAME);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_expression_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_EXPRESSION_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_struct_definition(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_STRUCT_DEFINITION);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_typedef_declaration(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TYPEDEF_DECLARATION);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_keyword(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_KEYWORD, node);
    if (ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }
    /* TODO: make this work for all keywords. Grammar and possibly code gen will need to be updated as well. */
    if (strcmp(ast_node->data.terminal.text, "struct") == 0)
    {
        ast_node->keyword = KEYWORD_STRUCT;
    }
    else if (strcmp(ast_node->data.terminal.text, "union") == 0)
    {
        ast_node->keyword = KEYWORD_UNION;
    }
    else
    {
        epc_ast_builder_set_error(
            ctx, "%s: Unknown keyword: %s", get_node_type_name_from_type(AST_NODE_KEYWORD), ast_node->data.terminal.text
        );
        c_grammar_node_free(ast_node, user_data);
        return;
    }

    epc_ast_push(ctx, ast_node);
}

void
c_grammar_ast_hook_registry_init(epc_ast_hook_registry_t * registry)
{
    epc_ast_hook_registry_set_free_node(registry, c_grammar_node_free);

    epc_ast_hook_registry_set_action(registry, AST_ACTION_IDENTIFIER, handle_identifier);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_INTEGER_BASE, handle_integer_base);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_FLOAT_BASE, handle_float_base);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_INTEGER_VALUE, handle_integer_literal);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_FLOAT_VALUE, handle_float_literal);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_STRING_LITERAL, handle_string_literal);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_CHARACTER_LITERAL, handle_character_literal);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_LITERAL_SUFFIX, handle_literal_suffix);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_FUNCTION_CALL, handle_function_call);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_POSTFIX_OPERATOR, handle_postfix_operator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_POSTFIX_EXPRESSION, handle_postfix_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ARRAY_SUBSCRIPT, handle_array_index);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_MEMBER_ACCESS_DOT, handle_member_access_dot);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_MEMBER_ACCESS_ARROW, handle_member_access_arrow);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_UNARY_OPERATOR, handle_unary_operator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_UNARY_EXPRESSION, handle_unary_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_CAST_EXPRESSION, handle_cast_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_RELATIONAL_OPERATOR, handle_relational_operator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_RELATIONAL, handle_relational_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_EQUALITY_OPERATOR, handle_equality_operator);
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
    epc_ast_hook_registry_set_action(registry, AST_ACTION_SHIFT_OPERATOR, handle_shift_operator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_SHIFT_EXPRESSION, handle_shift_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ARITHMETIC_OPERATOR, handle_arithmetic_operator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ARITHMETIC_EXPRESSION, handle_arithmetic_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ASSIGNMENT_OPERATOR, handle_assignment_operator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ASSIGNMENT, handle_assignment);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TYPE_SPECIFIER, handle_type_specifier);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DECL_SPECIFIERS, handle_decl_specifiers);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_POINTER, handle_pointer);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DIRECT_DECLARATOR, handle_direct_declarator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DECLARATOR_SUFFIX, handle_declarator_suffix);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DECLARATOR, handle_declarator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_OPTIONAL_KW_EXTENSION, handle_optional_kw_extension);
    epc_ast_hook_registry_set_action(
        registry, AST_ACTION_OPTIONAL_INIT_DECLARATOR_LIST, handle_optional_init_declarator_list
    );
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DECLARATION, handle_declaration);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_IF_STATEMENT, handle_if_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_SWITCH_STATEMENT, handle_switch_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_WHILE_STATEMENT, handle_while_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DO_WHILE_STATEMENT, handle_do_while_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_FOR_STATEMENT, handle_for_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_LABELED_STATEMENT, handle_labeled_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_LABELED_IDENTIFIER, handle_labeled_identifier);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_CASE_LABEL, handle_case_label);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_SWITCH_CASE, handle_switch_case);
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
    epc_ast_hook_registry_set_action(registry, AST_ACTION_POSTFIX_PARTS, handle_postfix_parts);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TYPEDEF_DECLARATION, handle_typedef_declaration);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_KEYWORD, handle_keyword);
}
