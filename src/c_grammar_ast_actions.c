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

    free(node->text);

    free_ast_node_children((void **)node->list.children, node->list.count, user_data);
    free(node->list.children);

    free((char *)node->op.text);
    c_grammar_node_free((c_grammar_node_t *)node->lhs, user_data);
    c_grammar_node_free((c_grammar_node_t *)node->rhs, user_data);
    c_grammar_node_free((c_grammar_node_t *)node->false_expr, user_data);

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
    node->list.count = (size_t)count;
    if (count > 0)
    {
        node->list.children = calloc((size_t)count, sizeof(*node->list.children));
        if (node->list.children == NULL)
        {
            free(node);
            return NULL;
        }
        for (int i = 0; i < count; i++)
        {
            node->list.children[i] = (c_grammar_node_t *)children[i];
        }
    }

    return node;
}

static c_grammar_node_t *
create_terminal_node(epc_ast_builder_ctx_t * ctx, c_grammar_node_type_t type, epc_cpt_node_t * node)
{
    c_grammar_node_t * ast_node = calloc(1, sizeof(*ast_node));
    if (ast_node == NULL)
    {
        epc_ast_builder_set_error(ctx, "%s: Memory allocation failed", get_node_type_name_from_type(type));
        return NULL;
    }

    ast_node->type = type;

    char const * text = epc_cpt_node_get_semantic_content(node);
    size_t text_len = epc_cpt_node_get_semantic_len(node);
    ast_node->text = strndup(text, text_len);
    if (ast_node->text == NULL)
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
handle_preprocessor_directive(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_PREPROCESSOR_DIRECTIVE);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_top_level_declaration(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "%s expected 2 children, but got %u\n", count);
        return;
    }
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TOP_LEVEL_DECLARATION);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->top_level_declaration.extension = ast_node->list.children[0];
    ast_node->top_level_declaration.declaration = ast_node->list.children[1];

    epc_ast_push(ctx, ast_node);
}

static void
handle_external_declaration(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 1)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 1 child, but got %d", get_node_type_name_from_type(AST_NODE_EXTERNAL_DECLARATION), count
        );
        return;
    }
    c_grammar_node_t const * child = children[0];
    if (child->type != AST_NODE_TOP_LEVEL_DECLARATION && child->type != AST_NODE_PREPROCESSOR_DIRECTIVE)
    {
        epc_ast_builder_set_error(
            ctx,
            "%s expected child of type %s or %s, but got %s",
            get_node_type_name_from_type(AST_NODE_EXTERNAL_DECLARATION),
            get_node_type_name_from_type(AST_NODE_TOP_LEVEL_DECLARATION),
            get_node_type_name_from_type(AST_NODE_PREPROCESSOR_DIRECTIVE),
            get_node_type_name_from_type(child->type)
        );
        free_ast_node_children(children, count, user_data);
        return;
    }

    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_EXTERNAL_DECLARATION);
    if (ast_node == NULL)
    {
        return;
    }
    if (child->type == AST_NODE_TOP_LEVEL_DECLARATION)
    {
        ast_node->external_declaration.top_level_declaration = child;
    }
    else
    {
        ast_node->external_declaration.preprocessor_directive = child;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_external_declarations(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_EXTERNAL_DECLARATIONS);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_translation_unit(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 1)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 1 child, but got %d", get_node_type_name_from_type(AST_NODE_TRANSLATION_UNIT), count
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TRANSLATION_UNIT);
    if (ast_node == NULL)
    {
        return;
    }
    ast_node->translation_unit.external_declarations = ast_node->list.children[0];

    epc_ast_push(ctx, ast_node);
}

static void
handle_function_definition(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 3)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 3 children, but got %d", get_node_type_name_from_type(AST_NODE_FUNCTION_DEFINITION), count
        );
        return;
    }
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_FUNCTION_DEFINITION);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->function_definition.declaration_specifiers = ast_node->list.children[0];
    ast_node->function_definition.declarator = ast_node->list.children[1];
    ast_node->function_definition.body = ast_node->list.children[2];

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
handle_asm_statement(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_ASM_STATEMENT);
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

    ast_node->declaration.extension = ast_node->list.children[0];
    ast_node->declaration.declaration_specifiers = ast_node->list.children[1];
    ast_node->declaration.init_declarator_list = ast_node->list.children[2];

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
    c_grammar_node_t * _ast_node = create_terminal_node(ctx, AST_NODE_INTEGER_LITERAL, node);
    if (_ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }
    ast_node_integer_literal_t * ast_node = &_ast_node->integer_lit;

    // Parse with base 0 to automatically handle 0x (hex) and 0 (octal)
    ast_node->integer_literal.value = strtoull(ast_node->base.text, NULL, 0);

    if (count == 2)
    {
        c_grammar_node_t * suffix_node = children[1];
        char * suffix_text = suffix_node->text;

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
    c_grammar_node_t * _ast_node = create_terminal_node(ctx, AST_NODE_FLOAT_LITERAL, node);
    if (_ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }
    ast_node_float_literal_t * ast_node = &_ast_node->float_lit;

    c_grammar_node_t * suffix_node = count == 2 ? children[1] : NULL;
    char * full_text = ast_node->base.text;

    ast_node->float_literal.value = strtold(full_text, NULL);
    ast_node->float_literal.type = FLOAT_LITERAL_TYPE_DOUBLE; /* Default to double. */
    if (suffix_node != NULL)
    {
        char * suffix_text = suffix_node->text;

        if (suffix_text != NULL)
        {
            if (strchr(suffix_text, 'f') || strchr(suffix_text, 'F'))
            {
                ast_node->float_literal.type = FLOAT_LITERAL_TYPE_FLOAT;
            }
            else if (strchr(suffix_text, 'l') || strchr(suffix_text, 'L'))
            {
                ast_node->float_literal.type = FLOAT_LITERAL_TYPE_LONG_DOUBLE;
            }
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

    char const * text = ast_node->text;

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
    ast_node->op.text = op_node->text;
    op_node->text = NULL;
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

    char const * op_text = ast_node->text;

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
    ast_node->op.text = op_node->text;
    op_node->text = NULL;
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

    char const * op_text = ast_node->text;

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
    ast_node->op.text = op_node->text;
    op_node->text = NULL;
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

    char const * op_text = ast_node->text;

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
    ast_node->op.text = op_node->text;
    op_node->text = NULL;
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

    char const * op_text = ast_node->text;

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
    ast_node->op.text = op_node->text;
    op_node->text = NULL;
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

    char const * op_text = ast_node->text;
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
    ast_node->op.text = op_node->text;
    op_node->text = NULL;
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

    char const * text = ast_node->text;

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
    if (0 && count != 2) /* Expecting [PrimaryExpression][Postfix Parts placeholder] */
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
            "%s expected postfix parts node at index 1, but got %s (%u)",
            get_node_type_name_from_type(AST_NODE_POSTFIX_EXPRESSION),
            get_node_type_name_from_node(postfix),
            postfix->type
        );
        return;
    }

    /*
       If the postfix parts list is empty then we just have a plain expression, so don't bother with a postfix
       expression node.
    */
    if (postfix->list.count == 0)
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
    if (count < 1)
    {
        epc_ast_builder_set_error(
            ctx, "%s at least 1 child but got none", get_node_type_name_from_type(AST_NODE_INIT_DECLARATOR)
        );
        free_ast_node_children(children, count, user_data);
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_INIT_DECLARATOR);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->init_declarator.declarator = children[0];
    /* There might be other nodes before the initializer node. */
    if (count > 1)
    {
        ast_node->init_declarator.initializer = children[count - 1];
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
handle_initializer(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_INITIALIZER);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_if_statement(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count < 2 || count > 3)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 or 3 children, but got %u", get_node_type_name_from_type(AST_NODE_IF_STATEMENT), count
        );
        return;
    }
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_IF_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->if_statement.condition = children[0];
    ast_node->if_statement.then_statement = children[1];
    if (count == 3)
    {
        ast_node->if_statement.else_statement = children[2];
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_switch_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %u", get_node_type_name_from_type(AST_NODE_SWITCH_STATEMENT), count
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_SWITCH_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->switch_statement.expression = children[0];
    ast_node->switch_statement.body = children[1];

    epc_ast_push(ctx, ast_node);
}

static void
handle_while_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %d", get_node_type_name_from_type(AST_NODE_WHILE_STATEMENT), count
        );
        return;
    }
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_WHILE_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->while_statement.condition = ast_node->list.children[0];
    ast_node->while_statement.body = ast_node->list.children[1];

    epc_ast_push(ctx, ast_node);
}

static void
handle_do_while_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %d", get_node_type_name_from_type(AST_NODE_DO_WHILE_STATEMENT), count
        );
        return;
    }
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DO_WHILE_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->do_while_statement.body = ast_node->list.children[0];
    ast_node->do_while_statement.condition = ast_node->list.children[1];

    epc_ast_push(ctx, ast_node);
}

static void
handle_for_statement(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count != 3 && count != 4)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 3 or 4 children, but got %d", get_node_type_name_from_type(AST_NODE_FOR_STATEMENT), count
        );
        return;
    }
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_FOR_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->for_statement.init = ast_node->list.children[0];
    ast_node->for_statement.condition = ast_node->list.children[1];
    ast_node->for_statement.post = (count == 4) ? ast_node->list.children[2] : NULL;
    ast_node->for_statement.body = (count == 4) ? ast_node->list.children[3] : ast_node->list.children[2];

    epc_ast_push(ctx, ast_node);
}

static void
handle_labeled_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %u", get_node_type_name_from_type(AST_NODE_LABELED_STATEMENT), count
        );
        return;
    }

    if (((c_grammar_node_t *)children[0])->type != AST_NODE_IDENTIFIER)
    {
        epc_ast_builder_set_error(
            ctx,
            "%s expected label node at index 0, but got %s",
            get_node_type_name_from_type(AST_NODE_LABELED_STATEMENT),
            get_node_type_name_from_node((c_grammar_node_t *)children[0])
        );

        free_ast_node_children(children, count, user_data);
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_LABELED_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->labeled_statement.label = children[0];
    ast_node->labeled_statement.statement = children[1];

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
handle_case_labels(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count == 0)
    {
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_CASE_LABELS);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_switch_case(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %u", get_node_type_name_from_type(AST_NODE_SWITCH_CASE), count
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_SWITCH_CASE);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->switch_case.labels = children[0];
    ast_node->switch_case.statements = children[1];

    epc_ast_push(ctx, ast_node);
}

static void
handle_default_statement(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 1)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 1 child, but got %u", get_node_type_name_from_type(AST_NODE_DEFAULT_STATEMENT), count
        );
        return;
    }
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DEFAULT_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->switch_case.statements = children[0];

    epc_ast_push(ctx, ast_node);
}

static void
handle_switch_body_statements(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_SWITCH_BODY_STATEMENTS);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_goto_statement(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count != 1)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 1 child, but got %d", get_node_type_name_from_type(AST_NODE_GOTO_STATEMENT), count
        );
        return;
    }
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_GOTO_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->goto_statement.label = ast_node->list.children[0];

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
            ctx,
            "%s expected 0 or 1 children, but got %d",
            get_node_type_name_from_type(AST_NODE_RETURN_STATEMENT),
            count
        );
        return;
    }
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_RETURN_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    if (count == 1)
    {
        ast_node->return_statement.expression = ast_node->list.children[0];
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
    if (count > 1)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected 0 or 1 children, but got %d",
            get_node_type_name_from_type(AST_NODE_EXPRESSION_STATEMENT),
            count
        );
        return;
    }
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_EXPRESSION_STATEMENT);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->expression_statement.expression = (count > 0) ? ast_node->list.children[0] : NULL;

    epc_ast_push(ctx, ast_node);
}

static void
handle_struct_declaration(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count < 2 || count > 3)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected 2 or 3 children, but got %u",
            get_node_type_name_from_type(AST_NODE_STRUCT_DECLARATION),
            count
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_STRUCT_DECLARATION);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->struct_declaration.extension = children[0];
    ast_node->struct_declaration.specifier_qualifier_list = children[1];
    if (count == 3)
    {
        ast_node->struct_declaration.declarator_list = children[2];
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_struct_declaration_list(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_STRUCT_DECLARATION_LIST);
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
handle_union_definition(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_UNION_DEFINITION);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_struct_type_ref(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %d", get_node_type_name_from_type(AST_NODE_STRUCT_TYPE_REF), count
        );
        return;
    }
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_STRUCT_TYPE_REF);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->type_ref.attribute_list = ast_node->list.children[0];
    ast_node->type_ref.identifier = ast_node->list.children[1];

    epc_ast_push(ctx, ast_node);
}

static void
handle_union_type_ref(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %d", get_node_type_name_from_type(AST_NODE_UNION_TYPE_REF), count
        );
        return;
    }
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_UNION_TYPE_REF);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->type_ref.attribute_list = ast_node->list.children[0];
    ast_node->type_ref.identifier = ast_node->list.children[1];

    epc_ast_push(ctx, ast_node);
}

static void
handle_enum_type_ref(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %d", get_node_type_name_from_type(AST_NODE_ENUM_TYPE_REF), count
        );
        return;
    }
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_ENUM_TYPE_REF);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->type_ref.attribute_list = ast_node->list.children[0];
    ast_node->type_ref.identifier = ast_node->list.children[1];

    epc_ast_push(ctx, ast_node);
}

static void
handle_enumerator(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_ENUMERATOR);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_function_pointer_declarator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_FUNCTION_POINTER_DECLARATOR);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_enum_definition(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_ENUM_DEFINITION);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_typedef_init_declarator_list(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TYPEDEF_INIT_DECLARATION_LIST);
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
    if (count != 3)
    {
        epc_ast_builder_set_error(
            ctx, "%s expected 3 children, but got %u", get_node_type_name_from_type(AST_NODE_TYPEDEF_DECLARATION), count
        );
        return;
    }
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TYPEDEF_DECLARATION);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->typedef_declaration.extension = children[0];
    ast_node->typedef_declaration.declaration_specifiers = children[1];
    ast_node->typedef_declaration.init_declarator_list = children[2];

    epc_ast_push(ctx, ast_node);
}

static void
handle_ternary_operation(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TERNARY_OPERATION);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_conditional_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count == 1)
    {
        epc_ast_push(ctx, children[0]);
        return;
    }

    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected 2 children, but got %u",
            get_node_type_name_from_type(AST_NODE_CONDITIONAL_EXPRESSION),
            count
        );
        return;
    }

    // Create the AST node - store all three in the list for IR generator to access
    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_CONDITIONAL_EXPRESSION, node);
    if (ast_node == NULL)
    {
        free_ast_node_children(children, count, user_data);
        return;
    }

    // Ternary operation present: a ? b : c
    c_grammar_node_t * ternary = children[1];

    // TernaryOperation children: [AssignmentExpression, ConditionalExpression]
    // children[0] = LogicalOrExpression (condition)
    // ternary->list.children[0] = true expression (AssignmentExpression)
    // ternary->list.children[1] = false expression (ConditionalExpression)

    c_grammar_node_t * condition = children[0];
    c_grammar_node_t * true_expr = ternary->list.children[0];
    c_grammar_node_t * false_expr = ternary->list.children[1];
    ternary->list.count = 0;

    /* Ownership of the ternary child nodes has been transferred to the ConditionalExression node, so it can be freed
     * now. */
    c_grammar_node_free(ternary, user_data);

    // Also set lhs/rhs for consistency with other binary ops
    ast_node->lhs = condition;
    ast_node->rhs = true_expr;
    ast_node->false_expr = false_expr;

    epc_ast_push(ctx, ast_node);
}

static void
handle_comma_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count == 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected at least 1 child, but got 0", get_node_type_name_from_type(AST_NODE_COMMA_EXPRESSION)
        );
        return;
    }

    if (count == 1)
    {
        /* Single expression, no comma - push back directly. */
        epc_ast_push(ctx, children[0]);
        return;
    }

    /* Multiple expressions - create a list node to hold them all. */
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_COMMA_EXPRESSION);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_designation(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DESIGNATION);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_compound_literal(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_COMPOUND_LITERAL);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_struct_declarator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_STRUCT_DECLARATOR);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_struct_declarator_list(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_STRUCT_DECLARATOR_LIST);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_struct_specifier_qualifier_list(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_STRUCT_SPECIFIER_QUALIFIER_LIST);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_struct_declarator_bitfield(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_STRUCT_DECLARATOR_BITFIELD);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_attribute_list(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_ATTRIBUTE_LIST);
    if (ast_node == NULL)
    {
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
    epc_ast_hook_registry_set_action(registry, AST_ACTION_CASE_LABEL, handle_case_label);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_CASE_LABELS, handle_case_labels);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_SWITCH_CASE, handle_switch_case);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_SWITCH_BODY_STATEMENTS, handle_switch_body_statements);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DEFAULT_STATEMENT, handle_default_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_GOTO_STATEMENT, handle_goto_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_CONTINUE_STATEMENT, handle_continue_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_BREAK_STATEMENT, handle_break_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_RETURN_STATEMENT, handle_return_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_COMPOUND_STATEMENT, handle_compound_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ASM_STATEMENT, handle_asm_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_FUNCTION_DEFINITION, handle_function_definition);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_PREPROCESSOR_DIRECTIVE, handle_preprocessor_directive);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_EXTERNAL_DECLARATIONS, handle_external_declarations);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_EXTERNAL_DECLARATION, handle_external_declaration);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TOP_LEVEL_DECLARATION, handle_top_level_declaration);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TRANSLATION_UNIT, handle_translation_unit);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_INIT_DECLARATOR, handle_init_declarator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_INITIALIZER_LIST, handle_initializer_list);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_INITIALIZER, handle_initializer);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TYPE_NAME, handle_type_name);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_EXPRESSION_STATEMENT, handle_expression_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_STRUCT_DECLARATION, handle_struct_declaration);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_STRUCT_DECLARATION_LIST, handle_struct_declaration_list);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_STRUCT_DEFINITION, handle_struct_definition);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_UNION_DEFINITION, handle_union_definition);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_POSTFIX_PARTS, handle_postfix_parts);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TYPEDEF_DECLARATION, handle_typedef_declaration);
    epc_ast_hook_registry_set_action(
        registry, AST_ACTION_TYPEDEF_INIT_DECLARATOR_LIST, handle_typedef_init_declarator_list
    );
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TERNARY_OPERATION, handle_ternary_operation);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_CONDITIONAL_EXPRESSION, handle_conditional_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_COMMA_EXPRESSION, handle_comma_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ENUM_DEFINITION, handle_enum_definition);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ENUMERATOR, handle_enumerator);
    epc_ast_hook_registry_set_action(
        registry, AST_ACTION_FUNCTION_POINTER_DECLARATOR, handle_function_pointer_declarator
    );
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DESIGNATION, handle_designation);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_COMPOUND_LITERAL, handle_compound_literal);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_STRUCT_DECLARATOR, handle_struct_declarator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_STRUCT_DECLARATOR_LIST, handle_struct_declarator_list);
    epc_ast_hook_registry_set_action(
        registry, AST_ACTION_STRUCT_SPECIFIER_QUALIFIER_LIST, handle_struct_specifier_qualifier_list
    );
    epc_ast_hook_registry_set_action(
        registry, AST_ACTION_STRUCT_DECLARATOR_BITFIELD, handle_struct_declarator_bitfield
    );
    epc_ast_hook_registry_set_action(registry, AST_ACTION_STRUCT_TYPE_REF, handle_struct_type_ref);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_UNION_TYPE_REF, handle_union_type_ref);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ENUM_TYPE_REF, handle_enum_type_ref);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ATTRIBUTE_LIST, handle_attribute_list);
}
