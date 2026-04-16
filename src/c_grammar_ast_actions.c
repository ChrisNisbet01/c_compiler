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

    free((char *)node->text);

    free_ast_node_children((void **)node->list.children, node->list.count, user_data);
    free(node->list.children);

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

static char const *
extract_node_text(epc_cpt_node_t * node)
{
    char const * text = epc_cpt_node_get_semantic_content(node);
    size_t text_len = epc_cpt_node_get_semantic_len(node);
    return strndup(text, text_len);
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

    ast_node->text = extract_node_text(node);
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
handle_init_declarator_list(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_INIT_DECLARATOR_LIST);
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
    if (count != 2 && count != 3)
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
    if (count == 3)
    {
        ast_node->declaration.init_declarator_list = ast_node->list.children[2];
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
    ast_node->integer_lit.integer_literal.value = strtoull(ast_node->text, NULL, 0);

    if (count == 2)
    {
        c_grammar_node_t * suffix_node = children[1];
        char const * suffix_text = suffix_node->text;

        if (suffix_text != NULL)
        {
            if (strchr(suffix_text, 'u') || strchr(suffix_text, 'U'))
            {
                ast_node->integer_lit.integer_literal.is_unsigned = true;
            }
            if (strchr(suffix_text, 'l') || strchr(suffix_text, 'L'))
            {
                ast_node->integer_lit.integer_literal.is_long = true;
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
    char const * full_text = ast_node->text;

    ast_node->float_lit.float_literal.value = strtold(full_text, NULL);
    ast_node->float_lit.float_literal.type = FLOAT_LITERAL_TYPE_DOUBLE; /* Default to double. */
    if (suffix_node != NULL)
    {
        char const * suffix_text = suffix_node->text;

        if (suffix_text != NULL)
        {
            if (strchr(suffix_text, 'f') || strchr(suffix_text, 'F'))
            {
                ast_node->float_lit.float_literal.type = FLOAT_LITERAL_TYPE_FLOAT;
            }
            else if (strchr(suffix_text, 'l') || strchr(suffix_text, 'L'))
            {
                ast_node->float_lit.float_literal.type = FLOAT_LITERAL_TYPE_LONG_DOUBLE;
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
handle_type_specifiers(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TYPE_SPECIFIERS);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_named_decl_specifiers(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count < 4)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected at least 4 children, but got %d",
            get_node_type_name_from_type(AST_NODE_NAMED_DECL_SPECIFIERS),
            count
        );
        return;
    }

    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_NAMED_DECL_SPECIFIERS);
    if (ast_node == NULL)
    {
        return;
    }

    size_t idx = 0;

    ast_node->decl_specifiers.storage_class = children[idx++];
    c_grammar_node_t * type_qualifiers_node = children[idx++];
    ast_node->decl_specifiers.type_qualifiers = type_qualifiers_node;

    if (type_qualifiers_node != NULL)
    {
        ast_node->decl_specifiers.type = type_qualifiers_node->type_qualifier;
    }

    c_grammar_node_t * third_child = children[idx];
    if (third_child != NULL && third_child->type == AST_NODE_FUNCTION_SPECIFIER)
    {
        ast_node->decl_specifiers.function_specifier = children[idx++];
    }

    ast_node->decl_specifiers.type_specifiers = children[idx++];
    if (ast_node->decl_specifiers.type_specifiers->type != AST_NODE_TYPE_SPECIFIERS)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected type specifiers node at index %u, but got %s",
            get_node_type_name_from_type(AST_NODE_NAMED_DECL_SPECIFIERS),
            idx - 1,
            get_node_type_name_from_node(ast_node->decl_specifiers.type_specifiers)
        );
        return;
    }

    c_grammar_node_t * child = children[idx];
    if (idx < (size_t)count && child != NULL && child->type == AST_NODE_TYPEDEF_SPECIFIER)
    {
        ast_node->decl_specifiers.typedef_name = children[idx++];
    }

    if (idx < (size_t)count)
    {
        ast_node->decl_specifiers.trailing_type_qualifiers = children[idx++];
        if (ast_node->decl_specifiers.trailing_type_qualifiers != NULL)
        {
            ast_node->decl_specifiers.type.is_const
                |= ast_node->decl_specifiers.trailing_type_qualifiers->type_qualifier.is_const;
            ast_node->decl_specifiers.type.is_volatile
                |= ast_node->decl_specifiers.trailing_type_qualifiers->type_qualifier.is_volatile;
            ast_node->decl_specifiers.type.is_restrict
                |= ast_node->decl_specifiers.trailing_type_qualifiers->type_qualifier.is_restrict;
        }
    }

    c_grammar_node_t const * storage_class_node = ast_node->decl_specifiers.storage_class;
    if (storage_class_node != NULL)
    {
        ast_node->decl_specifiers.storage = storage_class_node->storage_class;
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
            "%s expected assignment operator node at index 1, but got %s",
            get_node_type_name_from_type(AST_NODE_ASSIGNMENT),
            get_node_type_name_from_node(op_node)
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_ASSIGNMENT);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->binary_expression.left = children[0];
    ast_node->binary_expression.op = children[1];
    ast_node->binary_expression.right = children[2];

    epc_ast_push(ctx, ast_node);
}

static void
handle_typedef_specifier(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 1)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 1 child, but got %u", get_node_type_name_from_type(AST_NODE_TYPEDEF_SPECIFIER), count
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TYPEDEF_SPECIFIER);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->identifier.identifier = children[0];

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
handle_unary_expression_prefix(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected 2 children, but got %u",
            get_node_type_name_from_type(AST_NODE_UNARY_EXPRESSION_PREFIX),
            count
        );
        return;
    }

    /* The first child should always be the unary operator node. */
    c_grammar_node_t const * op_node = children[0];
    if (op_node->type != AST_NODE_UNARY_OPERATOR)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected operator node, but got %s",
            get_node_type_name_from_type(AST_NODE_UNARY_EXPRESSION_PREFIX),
            get_node_type_name_from_node(op_node)
        );
        return;
    }

    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_UNARY_EXPRESSION_PREFIX);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->unary_expression_prefix.op = children[0];
    ast_node->unary_expression_prefix.operand = children[1];

    epc_ast_push(ctx, ast_node);
}

static void
handle_declarator(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count != 4)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 4 children, but got %u", get_node_type_name_from_type(AST_NODE_DECLARATOR), count
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DECLARATOR);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->declarator.pointer_list = children[0];
    ast_node->declarator.direct_declarator = children[1];
    ast_node->declarator.declarator_suffix_list = children[2];
    ast_node->declarator.attribute_list = children[3];

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
handle_declarator_suffix_list(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DECLARATOR_SUFFIX_LIST);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_pointer(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_POINTER);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_pointer_list(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_POINTER_LIST);
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

    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_RELATIONAL_EXPRESSION);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->binary_expression.left = children[0];
    ast_node->binary_expression.op = children[1];
    ast_node->binary_expression.right = children[2];

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

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_EQUALITY_EXPRESSION);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->binary_expression.left = children[0];
    ast_node->binary_expression.op = children[1];
    ast_node->binary_expression.right = children[2];

    epc_ast_push(ctx, ast_node);
}

static void
handle_bitwise_operator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected no children, but got %u", get_node_type_name_from_type(AST_NODE_BITWISE_OPERATOR), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_BITWISE_OPERATOR, node);
    if (ast_node == NULL)
    {
        return;
    }

    char const * op_text = ast_node->text;

    if (strcmp(op_text, "&") == 0)
    {
        ast_node->op.bitwise.op = BITWISE_OP_AND;
    }
    else if (strcmp(op_text, "^") == 0)
    {
        ast_node->op.bitwise.op = BITWISE_OP_XOR;
    }
    else if (strcmp(op_text, "|") == 0)
    {
        ast_node->op.bitwise.op = BITWISE_OP_OR;
    }
    else
    {
        epc_ast_builder_set_error(
            ctx, "%s, Unknown operator: %s", get_node_type_name_from_type(AST_NODE_BITWISE_OPERATOR), op_text
        );
        c_grammar_node_free(ast_node, user_data);
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_bitwise_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 3)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %d", get_node_type_name_from_type(AST_NODE_BITWISE_EXPRESSION), count
        );
        return;
    }

    c_grammar_node_t const * op_node = children[1];

    if (op_node->type != AST_NODE_BITWISE_OPERATOR)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected bitwise operator node at index 1, but got %s",
            get_node_type_name_from_type(AST_NODE_BITWISE_EXPRESSION),
            get_node_type_name_from_node(op_node)
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_BITWISE_EXPRESSION);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->binary_expression.left = children[0];
    ast_node->binary_expression.op = children[1];
    ast_node->binary_expression.right = children[2];

    epc_ast_push(ctx, ast_node);
}

static void
handle_logical_operator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected no children, but got %u", get_node_type_name_from_type(AST_NODE_LOGICAL_OPERATOR), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_LOGICAL_OPERATOR, node);
    if (ast_node == NULL)
    {
        return;
    }

    char const * op_text = ast_node->text;

    if (strcmp(op_text, "&&") == 0)
    {
        ast_node->op.logical.op = LOGICAL_OP_AND;
    }
    else if (strcmp(op_text, "||") == 0)
    {
        ast_node->op.logical.op = LOGICAL_OP_OR;
    }
    else
    {
        epc_ast_builder_set_error(
            ctx, "%s, Unknown operator: %s", get_node_type_name_from_type(AST_NODE_LOGICAL_OPERATOR), op_text
        );
        c_grammar_node_free(ast_node, user_data);
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_logical_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 3)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %d", get_node_type_name_from_type(AST_NODE_LOGICAL_EXPRESSION), count
        );
        return;
    }

    c_grammar_node_t const * op_node = children[1];

    if (op_node->type != AST_NODE_LOGICAL_OPERATOR)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected bitwise operator node at index 1, but got %s",
            get_node_type_name_from_type(AST_NODE_LOGICAL_OPERATOR),
            get_node_type_name_from_node(op_node)
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_LOGICAL_EXPRESSION);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->binary_expression.left = children[0];
    ast_node->binary_expression.op = children[1];
    ast_node->binary_expression.right = children[2];

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

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_SHIFT_EXPRESSION);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->binary_expression.left = children[0];
    ast_node->binary_expression.op = children[1];
    ast_node->binary_expression.right = children[2];

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

    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_ARITHMETIC_EXPRESSION);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->binary_expression.left = children[0];
    ast_node->binary_expression.op = children[1];
    ast_node->binary_expression.right = children[2];

    epc_ast_push(ctx, ast_node);
}

static void
handle_optional_argument_list(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_OPTIONAL_ARGUMENT_LIST);
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
    if (count != 2) /* Expecting [PrimaryExpression Postfix Parts] */
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %u", get_node_type_name_from_type(AST_NODE_POSTFIX_EXPRESSION), count
        );
        return;
    }

    c_grammar_node_t const * base = children[0];
    c_grammar_node_t const * postfix = children[1];

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

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_POSTFIX_EXPRESSION);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->postfix_expression.base_expression = base;
    ast_node->postfix_expression.postfix_parts = postfix;

    epc_ast_push(ctx, ast_node);
}

static void
handle_array_subscript(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
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
    if (count != 1)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 1 child, but got %u", get_node_type_name_from_type(AST_NODE_MEMBER_ACCESS_DOT), count
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_MEMBER_ACCESS_DOT);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->identifier.identifier = children[0];

    epc_ast_push(ctx, ast_node);
}

static void
handle_member_access_arrow(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 1)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 1 child, but got %u", get_node_type_name_from_type(AST_NODE_MEMBER_ACCESS_DOT), count
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_MEMBER_ACCESS_ARROW);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->identifier.identifier = children[0];

    epc_ast_push(ctx, ast_node);
}

static void
handle_cast_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 4 or 5 children but got %u", get_node_type_name_from_type(AST_NODE_INIT_DECLARATOR), count
        );
        return;
    }

    c_grammar_node_t const * type_name_node = children[0];

    if (type_name_node->type != AST_NODE_TYPE_NAME)
    {
        epc_ast_builder_set_error(
            ctx,
            "%s expected first child to be %s, but got %s",
            get_node_type_name_from_type(AST_NODE_TYPE_NAME),
            get_node_type_name_from_type(type_name_node->type)
        );
        free_ast_node_children(children, count, user_data);
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_CAST_EXPRESSION);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->cast_expression.type_name = children[0];
    ast_node->cast_expression.expression = children[1];

    epc_ast_push(ctx, ast_node);
}

static void
handle_init_declarator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count < 3 || count > 5)
    {
        epc_ast_builder_set_error(
            ctx, "%s expected 4 or 5 children but got %u", get_node_type_name_from_type(AST_NODE_INIT_DECLARATOR), count
        );
        free_ast_node_children(children, count, user_data);
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_INIT_DECLARATOR);
    if (ast_node == NULL)
    {
        return;
    }

    size_t node_idx = 0;
    ast_node->init_declarator.declarator = children[node_idx++];
    ast_node->init_declarator.attribute_list_1 = children[node_idx++];

    c_grammar_node_t const * child;

    child = children[node_idx];
    if (child->type == AST_NODE_ASM_NAMES)
    {
        ast_node->init_declarator.optional_asm_name_list = children[node_idx++];
    }
    ast_node->init_declarator.attribute_list_2 = children[node_idx++];
    if (node_idx < (size_t)count)
    {
        ast_node->init_declarator.initializer = children[node_idx++];
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_initializer_list_entry(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count < 1 || count > 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected 1 or 2 children, but got %u",
            get_node_type_name_from_type(AST_NODE_INITIALIZER_LIST_ENTRY),
            count
        );
        return;
    }

    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_INITIALIZER_LIST_ENTRY);
    if (ast_node == NULL)
    {
        return;
    }

    if (count == 1)
    {
        ast_node->initializer_list_entry.initializer = children[0];
    }
    else
    {
        ast_node->initializer_list_entry.designation = children[0];
        ast_node->initializer_list_entry.initializer = children[1];
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
    if (count != 1 && count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 1 or 2 children, but got %d", get_node_type_name_from_type(AST_NODE_TYPE_NAME), count
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TYPE_NAME);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->type_name.specifier_qualifier_list = children[0];
    if (count == 2)
    {
        ast_node->type_name.abstract_declarator = children[1];
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
    if (count != 3 && count != 4)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected 3 or 4 children, but got %u",
            get_node_type_name_from_type(AST_NODE_STRUCT_DEFINITION),
            count
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_STRUCT_DEFINITION);
    if (ast_node == NULL)
    {
        return;
    }

    size_t idx = 0;

    ast_node->struct_definition.attribute_list_1 = children[idx++];
    if (count == 4)
    {
        ast_node->struct_definition.identifier = children[idx++];
    }
    ast_node->struct_definition.declaration_list = children[idx++];
    ast_node->struct_definition.attribute_list_2 = children[idx++];

    epc_ast_push(ctx, ast_node);
}

static void
handle_union_definition(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 3 && count != 4)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected 3 or 4 children, but got %u",
            get_node_type_name_from_type(AST_NODE_UNION_DEFINITION),
            count
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_UNION_DEFINITION);
    if (ast_node == NULL)
    {
        return;
    }

    size_t idx = 0;

    ast_node->struct_definition.attribute_list_1 = children[idx++];
    if (count == 4)
    {
        ast_node->struct_definition.identifier = children[idx++];
    }
    ast_node->struct_definition.declaration_list = children[idx++];
    ast_node->struct_definition.attribute_list_2 = children[idx++];

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
    if (count != 1 && count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 1 or 2 children, but got %u", get_node_type_name_from_type(AST_NODE_ENUMERATOR), count
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_ENUMERATOR);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->enumerator.identifier = children[0];
    if (count == 2)
    {
        ast_node->enumerator.expression = children[1];
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_function_pointer_declarator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 3)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected 3 children, but got %u",
            get_node_type_name_from_type(AST_NODE_FUNCTION_POINTER_DECLARATOR),
            count
        );
        return;
    }

    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_FUNCTION_POINTER_DECLARATOR);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->function_pointer_declarator.pointer = children[0];
    ast_node->function_pointer_declarator.identifier = children[1];
    ast_node->function_pointer_declarator.declarator_suffix_list = children[2];

    epc_ast_push(ctx, ast_node);
}

static void
handle_enumerator_list(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_ENUMERATOR_LIST);
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
    if (count != 3 && count != 4)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected 3 or 4 children, but got %u",
            get_node_type_name_from_type(AST_NODE_ENUM_DEFINITION),
            count
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_ENUM_DEFINITION);
    if (ast_node == NULL)
    {
        return;
    }

    size_t idx = 0;

    ast_node->enum_definition.attribute_list_1 = children[idx++];
    if (count == 4)
    {
        ast_node->enum_definition.identifier = children[idx++];
    }
    ast_node->enum_definition.enumerator_list = children[idx++];
    ast_node->enum_definition.attribute_list_2 = children[idx++];

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

    ast_node->declaration.extension = children[0];
    ast_node->declaration.declaration_specifiers = children[1];
    ast_node->declaration.init_declarator_list = children[2];

    epc_ast_push(ctx, ast_node);
}

static void
handle_typedef_direct_declarator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 1 && count != 2)
    {
        epc_ast_builder_set_error(
            ctx,
            "%s expected 1 or 2 children but got %u",
            get_node_type_name_from_type(AST_NODE_TYPEDEF_DIRECT_DECLARATOR),
            count
        );
        free_ast_node_children(children, count, user_data);
        return;
    }

    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TYPEDEF_DIRECT_DECLARATOR);
    if (ast_node == NULL)
    {
        return;
    }

    if (count == 2)
    {
        // TypedefDefiningIdentifier AttributeList
        ast_node->typedef_direct_declarator.identifier = children[0];
        ast_node->typedef_direct_declarator.attribute_list = children[1];
    }
    else
    {
        // LParen TypedefDeclarator RParen
        ast_node->typedef_direct_declarator.nested_typedef_declarator = children[0];
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_typedef_declarator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 4)
    {
        epc_ast_builder_set_error(
            ctx, "%s expected 4 children but got %u", get_node_type_name_from_type(AST_NODE_TYPEDEF_DECLARATOR), count
        );
        free_ast_node_children(children, count, user_data);
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TYPEDEF_DECLARATOR);
    if (ast_node == NULL)
    {
        return;
    }

    size_t node_idx = 0;
    ast_node->typedef_declarator.pointer_list = children[node_idx++];
    ast_node->typedef_declarator.direct_declarator = children[node_idx++];
    ast_node->typedef_declarator.declarator_suffix_list = children[node_idx++];
    ast_node->typedef_declarator.attribute_list = children[node_idx++];

    epc_ast_push(ctx, ast_node);
}

static void
handle_typedef_init_declarator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count < 3 || count > 5)
    {
        epc_ast_builder_set_error(
            ctx, "%s expected 4 or 5 children but got %u", get_node_type_name_from_type(AST_NODE_INIT_DECLARATOR), count
        );
        free_ast_node_children(children, count, user_data);
        return;
    }

    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TYPEDEF_INIT_DECLARATOR);
    if (ast_node == NULL)
    {
        return;
    }

    size_t node_idx = 0;
    ast_node->init_declarator.declarator = children[node_idx++];
    ast_node->init_declarator.attribute_list_1 = children[node_idx++];

    c_grammar_node_t const * child;

    child = children[node_idx];
    if (child->type == AST_NODE_ASM_NAMES)
    {
        ast_node->init_declarator.optional_asm_name_list = children[node_idx++];
    }
    ast_node->init_declarator.attribute_list_2 = children[node_idx++];
    if (node_idx < (size_t)count)
    {
        ast_node->init_declarator.initializer = children[node_idx++];
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_ternary_operation(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %u", get_node_type_name_from_type(AST_NODE_TERNARY_OPERATION), count
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TERNARY_OPERATION);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->ternary_operation.true_expression = children[0];
    ast_node->ternary_operation.false_expression = children[1];

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

    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_CONDITIONAL_EXPRESSION);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->conditional_expression.condition_expression = children[0];
    ast_node->conditional_expression.ternary_operation = children[1];

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
    if (count != 2)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected 2 children, but got %u", get_node_type_name_from_type(AST_NODE_COMPOUND_LITERAL), count
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_COMPOUND_LITERAL);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->compound_literal.type_name = children[0];
    ast_node->compound_literal.initializer_list = children[1];

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
handle_typedef_specifier_qualifier(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 3)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected 3 children, but got %u",
            get_node_type_name_from_type(AST_NODE_TYPEDEF_SPECIFIER_QUALIFIER),
            count
        );
        return;
    }

    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TYPEDEF_SPECIFIER_QUALIFIER);
    if (ast_node == NULL)
    {
        return;
    }

    ast_node->typedef_specifier_qualifier.pre_type_qualifier = children[0];
    ast_node->typedef_specifier_qualifier.typedef_specifier = children[1];
    ast_node->typedef_specifier_qualifier.post_type_qualifier = children[2];

    epc_ast_push(ctx, ast_node);
}

static void
handle_specifier_qualifier_list(
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
handle_attribute(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_ATTRIBUTE);
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

static void
handle_asm_names(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_ASM_NAMES);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_abstract_declarator(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_ABSTRACT_DECLARATOR);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_storage_class_specifier(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count > 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx,
            "%s expected no children, but got %u",
            get_node_type_name_from_type(AST_NODE_STORAGE_CLASS_SPECIFIER),
            count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_STORAGE_CLASS_SPECIFIER, node);
    if (ast_node == NULL)
    {
        return;
    }

    if (ast_node->text != NULL)
    {
        if (strcmp(ast_node->text, "static") == 0)
        {
            ast_node->storage_class.has_static = true;
        }
        else if (strcmp(ast_node->text, "extern") == 0)
        {
            ast_node->storage_class.has_extern = true;
        }
        else if (strcmp(ast_node->text, "auto") == 0)
        {
            ast_node->storage_class.has_auto = true;
        }
        else if (strcmp(ast_node->text, "register") == 0)
        {
            ast_node->storage_class.has_register = true;
        }
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_storage_class_specifiers(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_STORAGE_CLASS_SPECIFIERS);
    if (ast_node == NULL)
    {
        return;
    }

    for (int i = 0; i < count; i++)
    {
        c_grammar_node_t * child = (c_grammar_node_t *)children[i];
        ast_node->storage_class.has_static |= child->storage_class.has_static;
        ast_node->storage_class.has_extern |= child->storage_class.has_extern;
        ast_node->storage_class.has_auto |= child->storage_class.has_auto;
        ast_node->storage_class.has_register |= child->storage_class.has_register;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_function_specifier(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count > 0)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected no children, but got %u", get_node_type_name_from_type(AST_NODE_FUNCTION_SPECIFIER), count
        );
        return;
    }

    c_grammar_node_t * ast_node = create_terminal_node(ctx, AST_NODE_FUNCTION_SPECIFIER, node);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_type_qualifier(epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data)
{
    if (count > 1)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s expected no children, but got %u", get_node_type_name_from_type(AST_NODE_TYPE_QUALIFIER), count
        );
        return;
    }

    char const * node_text = extract_node_text(node);
    if (node_text == NULL)
    {
        free_ast_node_children(children, count, user_data);
        epc_ast_builder_set_error(
            ctx, "%s: Memory allocation failed", get_node_type_name_from_type(AST_NODE_TYPE_QUALIFIER)
        );
        return;
    }

    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TYPE_QUALIFIER);
    if (ast_node == NULL)
    {
        free((char *)node_text);
        return;
    }

    ast_node->text = node_text;
    if (strcmp(ast_node->text, "const") == 0)
    {
        ast_node->type_qualifier.is_const = true;
    }
    else if (strcmp(ast_node->text, "volatile") == 0)
    {
        ast_node->type_qualifier.is_volatile = true;
    }
    else if (strcmp(ast_node->text, "restrict") == 0)
    {
        ast_node->type_qualifier.is_restrict = true;
    }

    if (count > 0)
    {
        ast_node->type_qualifier.attribute = children[0];
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_type_qualifiers(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_TYPE_QUALIFERS);
    if (ast_node == NULL)
    {
        return;
    }

    for (int i = 0; i < count; i++)
    {
        c_grammar_node_t * child = (c_grammar_node_t *)children[i];
        ast_node->type_qualifier.is_const |= child->type_qualifier.is_const;
        ast_node->type_qualifier.is_volatile |= child->type_qualifier.is_volatile;
        ast_node->type_qualifier.is_restrict |= child->type_qualifier.is_restrict;
    }

    epc_ast_push(ctx, ast_node);
}

static void
handle_declaration_specifiers(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node
        = handle_list_node(ctx, node, children, count, user_data, AST_NODE_DECLARATION_SPECIFIERS);
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
    epc_ast_hook_registry_set_action(registry, AST_ACTION_OPTIONAL_ARGUMENT_LIST, handle_optional_argument_list);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_POSTFIX_OPERATOR, handle_postfix_operator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_POSTFIX_EXPRESSION, handle_postfix_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ARRAY_SUBSCRIPT, handle_array_subscript);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_MEMBER_ACCESS_DOT, handle_member_access_dot);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_MEMBER_ACCESS_ARROW, handle_member_access_arrow);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_UNARY_OPERATOR, handle_unary_operator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_UNARY_EXPRESSION_PREFIX, handle_unary_expression_prefix);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_CAST_EXPRESSION, handle_cast_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_RELATIONAL_OPERATOR, handle_relational_operator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_RELATIONAL, handle_relational_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_EQUALITY_OPERATOR, handle_equality_operator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_EQUALITY, handle_equality_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_BITWISE_OPERATOR, handle_bitwise_operator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_BITWISE_EXPRESSION, handle_bitwise_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_LOGICAL_OPERATOR, handle_logical_operator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_LOGICAL_EXPRESSION, handle_logical_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_SHIFT_OPERATOR, handle_shift_operator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_SHIFT_EXPRESSION, handle_shift_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ARITHMETIC_OPERATOR, handle_arithmetic_operator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ARITHMETIC_EXPRESSION, handle_arithmetic_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ASSIGNMENT_OPERATOR, handle_assignment_operator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ASSIGNMENT, handle_assignment);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TYPE_SPECIFIER, handle_type_specifier);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TYPEDEF_SPECIFIER, handle_typedef_specifier);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_NAMED_DECL_SPECIFIERS, handle_named_decl_specifiers);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TYPE_SPECIFIER_LIST, handle_type_specifiers);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_POINTER, handle_pointer);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_POINTER_LIST, handle_pointer_list);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DIRECT_DECLARATOR, handle_direct_declarator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DECLARATOR_SUFFIX, handle_declarator_suffix);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DECLARATOR_SUFFIX_LIST, handle_declarator_suffix_list);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DECLARATOR, handle_declarator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_OPTIONAL_KW_EXTENSION, handle_optional_kw_extension);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_INIT_DECLARATOR_LIST, handle_init_declarator_list);
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
    epc_ast_hook_registry_set_action(registry, AST_ACTION_INITIALIZER_LIST_ENTRY, handle_initializer_list_entry);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_INITIALIZER, handle_initializer);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TYPE_NAME, handle_type_name);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_EXPRESSION_STATEMENT, handle_expression_statement);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_STRUCT_DECLARATION, handle_struct_declaration);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_STRUCT_DECLARATION_LIST, handle_struct_declaration_list);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_STRUCT_DEFINITION, handle_struct_definition);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_UNION_DEFINITION, handle_union_definition);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_POSTFIX_PARTS, handle_postfix_parts);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TYPEDEF_DECLARATION, handle_typedef_declaration);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TYPEDEF_DECLARATOR, handle_typedef_declarator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TYPEDEF_DIRECT_DECLARATOR, handle_typedef_direct_declarator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TYPEDEF_INIT_DECLARATOR, handle_typedef_init_declarator);
    epc_ast_hook_registry_set_action(
        registry, AST_ACTION_TYPEDEF_INIT_DECLARATOR_LIST, handle_typedef_init_declarator_list
    );
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TERNARY_OPERATION, handle_ternary_operation);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_CONDITIONAL_EXPRESSION, handle_conditional_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_COMMA_EXPRESSION, handle_comma_expression);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ENUMERATOR, handle_enumerator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ENUMERATOR_LIST, handle_enumerator_list);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ENUM_DEFINITION, handle_enum_definition);
    epc_ast_hook_registry_set_action(
        registry, AST_ACTION_FUNCTION_POINTER_DECLARATOR, handle_function_pointer_declarator
    );
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DESIGNATION, handle_designation);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_COMPOUND_LITERAL, handle_compound_literal);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_STRUCT_DECLARATOR, handle_struct_declarator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_STRUCT_DECLARATOR_LIST, handle_struct_declarator_list);
    epc_ast_hook_registry_set_action(
        registry, AST_ACTION_TYPEDEF_SPECIFIER_QUALIFIER, handle_typedef_specifier_qualifier
    );
    epc_ast_hook_registry_set_action(registry, AST_ACTION_SPECIFIER_QUALIFIER_LIST, handle_specifier_qualifier_list);
    epc_ast_hook_registry_set_action(
        registry, AST_ACTION_STRUCT_DECLARATOR_BITFIELD, handle_struct_declarator_bitfield
    );
    epc_ast_hook_registry_set_action(registry, AST_ACTION_STRUCT_TYPE_REF, handle_struct_type_ref);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_UNION_TYPE_REF, handle_union_type_ref);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ENUM_TYPE_REF, handle_enum_type_ref);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ATTRIBUTE, handle_attribute);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ATTRIBUTE_LIST, handle_attribute_list);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ASM_NAMES, handle_asm_names);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_ABSTRACT_DECLARATOR, handle_abstract_declarator);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_STORAGE_CLASS_SPECIFIER, handle_storage_class_specifier);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_FUNCTION_SPECIFIER, handle_function_specifier);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_STORAGE_CLASS_SPECIFIERS, handle_storage_class_specifiers);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TYPE_QUALIFIER, handle_type_qualifier);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_TYPE_QUALIFIERS, handle_type_qualifiers);
    epc_ast_hook_registry_set_action(registry, AST_ACTION_DECLARATION_SPECIFIERS, handle_declaration_specifiers);
}
