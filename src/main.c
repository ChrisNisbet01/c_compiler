#include "c_grammar.h"
#include "callbacks.h"
#include "c_grammar_ast.h"
#include "c_grammar_ast_actions.h"

#include <easy_pc/easy_pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_ast(c_grammar_node_t *node, int indent)
{
    if (node == NULL)
        return;

    for (int i = 0; i < indent; i++)
        printf("  ");

    switch (node->type)
    {
    case AST_NODE_TRANSLATION_UNIT:
        printf("TranslationUnit (%zu children)\n", node->data.list.count);
        for (size_t i = 0; i < node->data.list.count; i++)
            print_ast(node->data.list.children[i], indent + 1);
        break;
    case AST_NODE_FUNCTION_DEFINITION:
        printf("FunctionDefinition (%zu children)\n", node->data.list.count);
        for (size_t i = 0; i < node->data.list.count; i++)
            print_ast(node->data.list.children[i], indent + 1);
        break;
    case AST_NODE_COMPOUND_STATEMENT:
        printf("CompoundStatement (%zu children)\n", node->data.list.count);
        for (size_t i = 0; i < node->data.list.count; i++)
            print_ast(node->data.list.children[i], indent + 1);
        break;
    case AST_NODE_DECLARATION:
        printf("Declaration (%zu children)\n", node->data.list.count);
        for (size_t i = 0; i < node->data.list.count; i++)
            print_ast(node->data.list.children[i], indent + 1);
        break;
    case AST_NODE_INTEGER_LITERAL:
        printf("IntegerLiteral: %s (%ld)\n", node->data.terminal.text, node->data.terminal.value);
        break;
    case AST_NODE_IDENTIFIER:
        printf("Identifier: %s\n", node->data.terminal.text);
        break;
    case AST_NODE_DECL_SPECIFIERS:
        printf("DeclarationSpecifiers (%zu children)\n", node->data.list.count);
        for (size_t i = 0; i < node->data.list.count; i++)
            print_ast(node->data.list.children[i], indent + 1);
        break;
    case AST_NODE_ASSIGNMENT:
        printf("Assignment (%zu children)\n", node->data.list.count);
        for (size_t i = 0; i < node->data.list.count; i++)
            print_ast(node->data.list.children[i], indent + 1);
        break;
    case AST_NODE_TYPE_SPECIFIER:
        printf("TypeSpecifier: %s\n", node->data.terminal.text);
        break;
    case AST_NODE_BINARY_OP:
        printf("BinaryOp (%zu children)\n", node->data.list.count);
        for (size_t i = 0; i < node->data.list.count; i++)
            print_ast(node->data.list.children[i], indent + 1);
        break;
    case AST_NODE_UNARY_OP:
        printf("UnaryOp (%zu children)\n", node->data.list.count);
        for (size_t i = 0; i < node->data.list.count; i++)
            print_ast(node->data.list.children[i], indent + 1);
        break;
    case AST_NODE_OPERATOR:
        printf("Operator: %s\n", node->data.terminal.text);
        break;
    case AST_NODE_DECLARATOR:
        printf("Declarator (%zu children)\n", node->data.list.count);
        for (size_t i = 0; i < node->data.list.count; i++)
            print_ast(node->data.list.children[i], indent + 1);
        break;
    case AST_NODE_DIRECT_DECLARATOR:
        printf("DirectDeclarator (%zu children)\n", node->data.list.count);
        for (size_t i = 0; i < node->data.list.count; i++)
            print_ast(node->data.list.children[i], indent + 1);
        break;
    case AST_NODE_DECLARATOR_SUFFIX:
        printf("DeclaratorSuffix (%zu children)\n", node->data.list.count);
        for (size_t i = 0; i < node->data.list.count; i++)
            print_ast(node->data.list.children[i], indent + 1);
        break;
    case AST_NODE_POINTER:
        printf("Pointer (%zu children)\n", node->data.list.count);
        for (size_t i = 0; i < node->data.list.count; i++)
            print_ast(node->data.list.children[i], indent + 1);
        break;
    default:
        printf("Unknown node type %d\n", node->type);
        break;
    }
}

// --- Symbol Table Implementation ---

// Initial capacity for dynamically sized symbol tables
#define INITIAL_SYMBOL_CAPACITY 16

typedef struct
{
    char **names;    // Dynamically allocated array of names
    size_t count;    // Current number of names
    size_t capacity; // Current capacity of the names array
} symbol_table_t;

symbol_table_t *
symbol_table_create()
{
    symbol_table_t *st = calloc(1, sizeof(symbol_table_t));
    if (st == NULL)
        return NULL;

    st->capacity = INITIAL_SYMBOL_CAPACITY;
    st->names = malloc(sizeof(char *) * st->capacity);
    if (st->names == NULL)
    {
        free(st);
        return NULL;
    }
    st->count = 0;
    return st;
}

void symbol_table_free(symbol_table_t *st)
{
    if (st == NULL)
    {
        return;
    }

    for (size_t i = 0; i < st->count; i++)
    {
        free(st->names[i]);
    }
    free(st->names); // Free the dynamically allocated array
    free(st);
}

void symbol_table_add(symbol_table_t *st, char const *name)
{
    if (st == NULL || name == NULL)
    {
        return;
    }

    // Check if name already exists
    for (size_t i = 0; i < st->count; i++)
    {
        if (strcmp(st->names[i], name) == 0)
        {
            // Already exists, do nothing
            return;
        }
    }

    // Resize if capacity is reached
    if (st->count >= st->capacity)
    {
        st->capacity *= 2;
        char **new_names = realloc(st->names, sizeof(*st->names) * st->capacity);
        if (new_names == NULL)
        {
            // Handle realloc failure, maybe print an error and stop or return.
            // For now, we'll assume it succeeds or the program might crash later.
            // A more robust solution would propagate an error.
            fprintf(stderr, "Error: Failed to resize symbol table names array.\n");
            return;
        }
        st->names = new_names;
    }

    st->names[st->count++] = strdup(name);
}

bool symbol_table_contains(symbol_table_t *st, char const *name)
{
    if (st == NULL || name == NULL)
    {
        return false;
    }
    if (st->count == 0)
    {
        return false;
    }

    for (size_t i = 0; i < st->count; i++)
    {
        if (strcmp(st->names[i], name) == 0)
        {
            return true;
        }
    }

    return false;
}

// --- Transactional Context ---

typedef struct
{
    symbol_table_t *symbols;  // For user-defined typedefs
    symbol_table_t *builtins; // For pre-registered built-in types
    char **pending;
    int pending_count;
    int pending_capacity;
    int *marker_stack;
    int marker_top;
    int marker_capacity;
} parse_session_ctx_t;

parse_session_ctx_t *
session_ctx_create()
{
    parse_session_ctx_t *ctx = calloc(1, sizeof(parse_session_ctx_t));
    if (ctx == NULL)
    {
        return NULL;
    }

    ctx->symbols = symbol_table_create();
    if (ctx->symbols == NULL)
    {
        free(ctx);
        return NULL;
    }

    ctx->builtins = symbol_table_create();
    if (ctx->builtins == NULL)
    {
        symbol_table_free(ctx->symbols);
        free(ctx);
        return NULL;
    }

    // Pre-register common built-in types that are often used without explicit typedefs
    // Based on the content of temp_main.c and typical C environments
    symbol_table_add(ctx->builtins, "__builtin_va_list");

    ctx->pending_capacity = 16;
    ctx->pending = malloc(sizeof(*ctx->pending) * ctx->pending_capacity);
    if (ctx->pending == NULL)
    {
        symbol_table_free(ctx->builtins);
        symbol_table_free(ctx->symbols);
        free(ctx);
        return NULL;
    }

    ctx->marker_capacity = 16;
    ctx->marker_stack = malloc(sizeof(*ctx->marker_stack) * ctx->marker_capacity);
    if (ctx->marker_stack == NULL)
    {
        // Clean up allocated resources before returning NULL
        for (int i = 0; i < ctx->pending_count; i++)
        {
            free(ctx->pending[i]); // Free any pending strings if allocated
        }
        free(ctx->pending);
        symbol_table_free(ctx->builtins);
        symbol_table_free(ctx->symbols);
        free(ctx);
        return NULL;
    }

    return ctx;
}

void session_ctx_free(parse_session_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    symbol_table_free(ctx->symbols);
    symbol_table_free(ctx->builtins); // Free the new builtins table
    for (int i = 0; i < ctx->pending_count; i++)
    {
        free(ctx->pending[i]);
    }
    free(ctx->pending);
    free(ctx->marker_stack);
    free(ctx);
}

void session_ctx_push_pending(parse_session_ctx_t *ctx, char const *name)
{
    if (ctx == NULL || name == NULL)
    {
        return;
    }
    if (ctx->pending_count >= ctx->pending_capacity)
    {
        ctx->pending_capacity *= 2;
        char **new_pending = realloc(ctx->pending, sizeof(*ctx->pending) * ctx->pending_capacity);
        if (new_pending == NULL)
        {
            fprintf(stderr, "Error: Failed to resize pending names array.\n");
            // In a real application, you might want to handle this more gracefully,
            // e.g., by returning an error code or exiting.
            return;
        }
        ctx->pending = new_pending;
    }
    ctx->pending[ctx->pending_count++] = strdup(name);
}

// --- GDL Callbacks and Predicates ---

bool is_typedef_name(epc_cpt_node_t *token, epc_parser_ctx_t *parse_ctx, void *parser_data)
{
    (void)parser_data;
    parse_session_ctx_t *session = (parse_session_ctx_t *)parse_ctx_get_user_ctx(parse_ctx);
    if (session == NULL)
    {
        return false;
    }

    char const *name = epc_cpt_node_get_semantic_content(token);
    size_t len = epc_cpt_node_get_semantic_len(token);

    char *name_copy = strndup(name, len);
    if (name_copy == NULL)
    {
        return false;
    }

    // Check both user-defined symbols and pre-registered built-ins
    bool found = symbol_table_contains(session->symbols, name_copy) || symbol_table_contains(session->builtins, name_copy);

    free(name_copy);
    return found;
}

// Inner Wrap: Capture an identifier that might be a typedef
static void
on_capture_entry(epc_parser_t *parser, epc_parser_ctx_t *parse_ctx, void *parser_data)
{
    (void)parser;
    (void)parse_ctx;
    (void)parser_data;
}

static bool
on_capture_exit(epc_parse_result_t result, epc_parser_ctx_t *parse_ctx, void *parser_data)
{
    (void)parser_data;
    if (result.is_error)
    {
        return true;
    }

    parse_session_ctx_t *session = (parse_session_ctx_t *)parse_ctx_get_user_ctx(parse_ctx);
    if (session == NULL)
    {
        return true;
    }

    char const *name = epc_cpt_node_get_semantic_content(result.data.success);
    size_t len = epc_cpt_node_get_semantic_len(result.data.success);
    char *name_copy = strndup(name, len);
    if (name_copy == NULL)
    {
        // Handle potential strndup failure
        return true;
    }

    session_ctx_push_pending(session, name_copy);
    free(name_copy);

    return true;
}

// Outer Wrap: Commit or Discard pending typedefs
static void
on_commit_entry(epc_parser_t *parser, epc_parser_ctx_t *parse_ctx, void *parser_data)
{
    (void)parser;
    (void)parser_data;
    parse_session_ctx_t *session = (parse_session_ctx_t *)parse_ctx_get_user_ctx(parse_ctx);
    if (session == NULL)
    {
        return;
    }

    if (session->marker_top >= session->marker_capacity)
    {
        session->marker_capacity *= 2;
        int *new_marker_stack = realloc(session->marker_stack, sizeof(*session->marker_stack) * session->marker_capacity);
        if (new_marker_stack == NULL)
        {
            fprintf(stderr, "Error: Failed to resize marker stack.\n");
            return;
        }
        session->marker_stack = new_marker_stack;
    }
    session->marker_stack[session->marker_top++] = session->pending_count;
}

static bool
on_commit_exit(epc_parse_result_t result, epc_parser_ctx_t *parse_ctx, void *parser_data)
{
    (void)parser_data;
    parse_session_ctx_t *session = (parse_session_ctx_t *)parse_ctx_get_user_ctx(parse_ctx);
    if (session == NULL || session->marker_top == 0)
    {
        return true;
    }

    int marker = session->marker_stack[--session->marker_top];

    if (!result.is_error)
    {
        // Success: Move pending to real table
        for (int i = marker; i < session->pending_count; i++)
        {
            symbol_table_add(session->symbols, session->pending[i]);
            printf("Committed typedef: '%s'\n", session->pending[i]);
            free(session->pending[i]);
        }
        session->pending_count = marker;
    }
    else
    {
        // Failure: Discard pending
        for (int i = marker; i < session->pending_count; i++)
        {
            free(session->pending[i]);
        }
        session->pending_count = marker;
    }

    return true;
}

epc_wrap_callbacks_t typedef_capture_callbacks = {on_capture_entry, on_capture_exit};
epc_wrap_callbacks_t typedef_commit_callbacks = {on_commit_entry, on_commit_exit};

// --- Main ---

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char const *filename = argv[1];
    printf("Attempting to parse C file: %s\n", filename);

    epc_parser_list *list = epc_parser_list_create();
    if (list == NULL)
    {
        fprintf(stderr, "Failed to create parser list.\n");
        return EXIT_FAILURE;
    }

    parse_session_ctx_t *session_ctx = session_ctx_create();
    if (session_ctx == NULL)
    {
        fprintf(stderr, "Failed to create session context.\n");
        epc_parser_list_free(list);
        return EXIT_FAILURE;
    }

    // create_c_grammar_parser is generated from c_grammar.gdl
    epc_parser_t *c_parser = create_c_grammar_parser(list);
    if (c_parser == NULL)
    {
        fprintf(stderr, "Failed to create C parser.\n");
        session_ctx_free(session_ctx);
        epc_parser_list_free(list);
        return EXIT_FAILURE;
    }

    // Parse from file with session context
    epc_parse_session_t session = epc_parse_file(c_parser, filename, session_ctx);

    if (session.result.is_error)
    {
        epc_parser_error_t *err = session.result.data.error;
        fprintf(stderr, "Parse Error: %s\n", err->message);
        fprintf(stderr, "At line %zu, col %zu\n", err->position.line + 1, err->position.col + 1);
        fprintf(stderr, "Expected: %s\n", err->expected);
        fprintf(stderr, "Found: %s\n", err->found);

        epc_parse_session_destroy(&session);
        session_ctx_free(session_ctx);
        epc_parser_list_free(list);
        return EXIT_FAILURE;
    }

    printf("Successfully parsed the C file!\n");

    // Print the CPT
    char *cpt_str = epc_cpt_to_string(session.internal_parse_ctx, session.result.data.success);
    if (cpt_str != NULL)
    {
        printf("Concrete Parse Tree:\n%s\n", cpt_str);
        free(cpt_str);
    }

    // Build the AST
    epc_ast_hook_registry_t *registry = epc_ast_hook_registry_create(C_GRAMMAR_AST_ACTION_COUNT__);
    if (registry != NULL)
    {
        c_grammar_ast_hook_registry_init(registry);
        epc_ast_result_t ast_result = epc_ast_build(session.result.data.success, registry, NULL);

        if (!ast_result.has_error)
        {
            printf("\nAbstract Syntax Tree:\n");
            print_ast((c_grammar_node_t *)ast_result.ast_root, 0);
            c_grammar_node_free(ast_result.ast_root, NULL);
        }
        else
        {
            fprintf(stderr, "AST Build Error: %s\n", ast_result.error_message);
        }
        epc_ast_hook_registry_free(registry);
    }
    else
    {
        fprintf(stderr, "Failed to create AST registry.\n");
    }

    epc_parse_session_destroy(&session);
    session_ctx_free(session_ctx);
    epc_parser_list_free(list);
    return EXIT_SUCCESS;
}
