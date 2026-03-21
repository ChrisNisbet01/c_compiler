#include "c_grammar.h"
#include "c_grammar_ast.h"
#include "c_grammar_ast_actions.h"
#include "callbacks.h"

// Include for LLVM IR Generator
#include "llvm_ir_generator.h"

#include <easy_pc/easy_pc.h>
#include <getopt.h>  // For getopt_long
#include <stdbool.h> // For bool type
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// --- Global variables to store parsed command-line options ---
static bool compile_only_flag = false;
static bool assembly_only_flag = false;
static bool emit_llvm_flag = false;
static char * output_filename = NULL;
static char * march_target = "x86-64";
static char * lib_names[64]; // -l flags (e.g., "m" for -lm)
static int lib_names_count = 0;
static char * lib_paths[64]; // -L flags (e.g., "/usr/local/lib")
static int lib_paths_count = 0;

typedef struct
{
    char const * name;
} node_type_name_t;

static void print_ast(c_grammar_node_t const * node, int indent);

static node_type_name_t const node_type_names[] = {
    [AST_NODE_TRANSLATION_UNIT] = {.name = "TranslationUnit"},
    [AST_NODE_FUNCTION_DEFINITION] = {.name = "FunctionDefinition"},
    [AST_NODE_COMPOUND_STATEMENT] = {.name = "CompoundStatement"},
    [AST_NODE_DECLARATION] = {.name = "Declaration"},
    [AST_NODE_INTEGER_BASE] = {.name = "IntegerLiteral"},
    [AST_NODE_FLOAT_BASE] = {.name = "FloatLiteral"},
    [AST_NODE_INTEGER_VALUE] = {.name = "IntegerValue"},
    [AST_NODE_FLOAT_VALUE] = {.name = "FloatValue"},
    [AST_NODE_STRING_LITERAL] = {.name = "StringLiteral"},
    [AST_NODE_LITERAL_SUFFIX] = {.name = "LiteralSuffix"},
    [AST_NODE_IDENTIFIER] = {.name = "Identifier"},
    [AST_NODE_DECL_SPECIFIERS] = {.name = "DeclarationSpecifiers"},
    [AST_NODE_ASSIGNMENT] = {.name = "Assignment"},
    [AST_NODE_TYPE_SPECIFIER] = {.name = "TypeSpecifier"},
    [AST_NODE_UNARY_OP] = {.name = "UnaryOp"},
    [AST_NODE_OPERATOR] = {.name = "Operator"},
    [AST_NODE_DECLARATOR] = {.name = "Declarator"},
    [AST_NODE_DIRECT_DECLARATOR] = {.name = "DirectDeclarator"},
    [AST_NODE_DECLARATOR_SUFFIX] = {.name = "DeclaratorSuffix"},
    [AST_NODE_POINTER] = {.name = "Pointer"},
    [AST_NODE_RELATIONAL_EXPRESSION] = {.name = "RelationalExpression"},
    [AST_NODE_EQUALITY_EXPRESSION] = {.name = "EqualityExpression"},
    [AST_NODE_BITWISE_EXPRESSION] = {.name = "BitwiseExpression"},
    [AST_NODE_LOGICAL_EXPRESSION] = {.name = "LogicalExpression"},
    [AST_NODE_SHIFT_EXPRESSION] = {.name = "ShiftExpression"},
    [AST_NODE_ARITHMETIC_EXPRESSION] = {.name = "ArithmeticExpression"},
    [AST_NODE_FUNCTION_CALL] = {.name = "FunctionCall"},
    [AST_NODE_POSTFIX_EXPRESSION] = {.name = "PostfixExpression"},
    [AST_NODE_POSTFIX_OPERATOR] = {.name = "PostfixOperator"},
    [AST_NODE_ARRAY_SUBSCRIPT] = {.name = "ArraySubscript"},
    [AST_NODE_MEMBER_ACCESS_DOT] = {.name = "MemberAccessDot"},
    [AST_NODE_MEMBER_ACCESS_ARROW] = {.name = "MemberAccessArrow"},
    [AST_NODE_CAST_EXPRESSION] = {.name = "CastExpression"},
    [AST_NODE_INIT_DECLARATOR] = {.name = "InitDeclarator"},
    [AST_NODE_IF_STATEMENT] = {.name = "IfStatement"},
    [AST_NODE_SWITCH_STATEMENT] = {.name = "SwitchStatement"},
    [AST_NODE_WHILE_STATEMENT] = {.name = "WhileStatement"},
    [AST_NODE_DO_WHILE_STATEMENT] = {.name = "DoWhileStatement"},
    [AST_NODE_FOR_STATEMENT] = {.name = "ForStatement"},
    [AST_NODE_GOTO_STATEMENT] = {.name = "GotoStatement"},
    [AST_NODE_CONTINUE_STATEMENT] = {.name = "ContinueStatement"},
    [AST_NODE_BREAK_STATEMENT] = {.name = "BreakStatement"},
    [AST_NODE_RETURN_STATEMENT] = {.name = "ReturnStatement"},
    [AST_NODE_TYPE_NAME] = {.name = "TypeName"},
    [AST_NODE_EXPRESSION_STATEMENT] = {.name = "ExpressionStatement"},
    [AST_NODE_STRUCT_DEFINITION] = {.name = "StructDefinition"},
    [AST_NODE_INITIALIZER_LIST] = {.name = "InitializerList"},
    [AST_NODE_LABELED_STATEMENT] = {.name = "LabeledStatement"},
    [AST_NODE_CHARACTER_LITERAL] = {.name = "CharacterLiteral"},
    [AST_NODE_CASE_LABEL] = {.name = "CaseLabel"},
    [AST_NODE_SWITCH_CASE] = {.name = "SwitchCase"},
    [AST_NODE_DEFAULT_STATEMENT] = {.name = "DefaultStatement"},
    [AST_NODE_LABELED_IDENTIFIER] = {.name = "LabeledIdentifier"},
    [AST_NODE_ASSIGNMENT_OPERATOR] = {.name = "AssignmentOperator"},
};

#define NUM_NODE_TYPE_NAMES ARRAY_SIZE(node_type_names)

static char const *
get_node_type_name(c_grammar_node_type_t const type)
{
    if (type < 0 || type >= NUM_NODE_TYPE_NAMES || node_type_names[type].name == NULL)
    {
        return "Unknown";
    }

    return node_type_names[type].name;
}

static void
print_list_type_ast_node(c_grammar_node_t const * node, int indent)
{
    printf("%s (%u): (%zu children)\n", get_node_type_name(node->type), node->type, node->data.list.count);
    for (size_t i = 0; i < node->data.list.count; i++)
        print_ast(node->data.list.children[i], indent + 1);
}

static void
print_ast(c_grammar_node_t const * node, int indent)
{
    if (node == NULL)
    {
        printf("NULL node\n");
        return;
    }

    for (int i = 0; i < indent; i++)
    {
        printf("  ");
    }

    if (node->is_terminal_node)
    {
        printf("%s (%u): %s\n", get_node_type_name(node->type), node->type, node->data.terminal.text);
    }
    else
    {
        print_list_type_ast_node(node, indent);
    }
}

// --- Symbol Table Implementation ---

// Initial capacity for dynamically sized symbol tables
#define INITIAL_SYMBOL_CAPACITY 16

typedef struct
{
    char ** names;   // Dynamically allocated array of names
    size_t count;    // Current number of names
    size_t capacity; // Current capacity of the names array
} symbol_table_t;

symbol_table_t *
symbol_table_create()
{
    symbol_table_t * st = calloc(1, sizeof(symbol_table_t));
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

void
symbol_table_free(symbol_table_t * st)
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

void
symbol_table_add(symbol_table_t * st, char const * name)
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
        char ** new_names = realloc(st->names, sizeof(*st->names) * st->capacity);
        if (new_names == NULL)
        {
            fprintf(stderr, "Error: Failed to resize symbol table names array.\n");
            return;
        }
        st->names = new_names;
    }

    st->names[st->count++] = strdup(name);
}

bool
symbol_table_contains(symbol_table_t * st, char const * name)
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
    symbol_table_t * symbols;  // For user-defined typedefs
    symbol_table_t * builtins; // For pre-registered built-in types
    char ** pending;
    int pending_count;
    int pending_capacity;
    int * marker_stack;
    int marker_top;
    int marker_capacity;
} parse_session_ctx_t;

parse_session_ctx_t *
session_ctx_create()
{
    parse_session_ctx_t * ctx = calloc(1, sizeof(parse_session_ctx_t));
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
        for (int i = 0; i < ctx->pending_count; i++)
        {
            free(ctx->pending[i]);
        }
        free(ctx->pending);
        symbol_table_free(ctx->builtins);
        symbol_table_free(ctx->symbols);
        free(ctx);
        return NULL;
    }

    return ctx;
}

void
session_ctx_free(parse_session_ctx_t * ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    symbol_table_free(ctx->symbols);
    symbol_table_free(ctx->builtins);
    for (int i = 0; i < ctx->pending_count; i++)
    {
        free(ctx->pending[i]);
    }
    free(ctx->pending);
    free(ctx->marker_stack);
    free(ctx);
}

void
session_ctx_push_pending(parse_session_ctx_t * ctx, char const * name)
{
    if (ctx == NULL || name == NULL)
    {
        return;
    }
    if (ctx->pending_count >= ctx->pending_capacity)
    {
        ctx->pending_capacity *= 2;
        char ** new_pending = realloc(ctx->pending, sizeof(*ctx->pending) * ctx->pending_capacity);
        if (new_pending == NULL)
        {
            fprintf(stderr, "Error: Failed to resize pending names array.\n");
            return;
        }
        ctx->pending = new_pending;
    }
    ctx->pending[ctx->pending_count++] = strdup(name);
}

// --- GDL Callbacks and Predicates ---

bool
is_typedef_name(epc_cpt_node_t * token, epc_parser_ctx_t * parse_ctx, void * parser_data)
{
    (void)parser_data;
    parse_session_ctx_t * session = (parse_session_ctx_t *)parse_ctx_get_user_ctx(parse_ctx);
    if (session == NULL)
    {
        return false;
    }

    char const * name = epc_cpt_node_get_semantic_content(token);
    size_t len = epc_cpt_node_get_semantic_len(token);

    char * name_copy = strndup(name, len);
    if (name_copy == NULL)
    {
        return false;
    }

    bool found
        = symbol_table_contains(session->symbols, name_copy) || symbol_table_contains(session->builtins, name_copy);

    free(name_copy);
    return found;
}

static void
on_capture_entry(epc_parser_t * parser, epc_parser_ctx_t * parse_ctx, void * parser_data)
{
    (void)parser;
    (void)parse_ctx;
    (void)parser_data;
}

static bool
on_capture_exit(epc_parse_result_t result, epc_parser_ctx_t * parse_ctx, void * parser_data)
{
    (void)parser_data;
    if (result.is_error)
    {
        return true;
    }

    parse_session_ctx_t * session = (parse_session_ctx_t *)parse_ctx_get_user_ctx(parse_ctx);
    if (session == NULL)
    {
        return true;
    }

    char const * name = epc_cpt_node_get_semantic_content(result.data.success);
    size_t len = epc_cpt_node_get_semantic_len(result.data.success);
    char * name_copy = strndup(name, len);
    if (name_copy == NULL)
    {
        return true;
    }

    session_ctx_push_pending(session, name_copy);
    free(name_copy);

    return true;
}

static void
on_commit_entry(epc_parser_t * parser, epc_parser_ctx_t * parse_ctx, void * parser_data)
{
    (void)parser;
    (void)parser_data;
    parse_session_ctx_t * session = (parse_session_ctx_t *)parse_ctx_get_user_ctx(parse_ctx);
    if (session == NULL)
    {
        return;
    }

    if (session->marker_top >= session->marker_capacity)
    {
        session->marker_capacity *= 2;
        int * new_marker_stack
            = realloc(session->marker_stack, sizeof(*session->marker_stack) * session->marker_capacity);
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
on_commit_exit(epc_parse_result_t result, epc_parser_ctx_t * parse_ctx, void * parser_data)
{
    (void)parser_data;
    parse_session_ctx_t * session = (parse_session_ctx_t *)parse_ctx_get_user_ctx(parse_ctx);
    if (session == NULL || session->marker_top == 0)
    {
        return true;
    }

    int marker = session->marker_stack[--session->marker_top];

    if (!result.is_error)
    {
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

static char *
derive_output_filename(char const * input_path, char const * ext)
{
    static char derived[1024];
    char const * base = strrchr(input_path, '/');
    base = base ? base + 1 : input_path;

    char const * dot = strrchr(base, '.');
    size_t base_len = dot ? (size_t)(dot - base) : strlen(base);

    char const * dir_end = strrchr(input_path, '/');
    size_t dir_len = dir_end ? (size_t)(dir_end - input_path + 1) : 0;

    memcpy(derived, input_path, dir_len);
    memcpy(derived + dir_len, base, base_len);
    snprintf(derived + dir_len + base_len, sizeof(derived) - dir_len - base_len, ".%s", ext);

    return derived;
}

static int
link_to_executable(LLVMModuleRef llvm_module, char const * o_path, char const * exe_path)
{
    // 1. Emit object file
    if (emit_to_file(llvm_module, o_path, march_target, LLVMObjectFile) != 0)
    {
        fprintf(stderr, "Failed to emit object file %s\n", o_path);
        return -1;
    }

    // 2. Build linker command
    char cmd[4096];
    int pos = snprintf(cmd, sizeof(cmd), "cc -no-pie %s", o_path);

    // Add library search paths
    for (int i = 0; i < lib_paths_count && pos < (int)sizeof(cmd) - 50; i++)
    {
        pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, " -L%s", lib_paths[i]);
    }

    // Add output filename
    pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, " -o %s", exe_path);

    // Add libraries (must come after -o)
    for (int i = 0; i < lib_names_count && pos < (int)sizeof(cmd) - 50; i++)
    {
        pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, " -l%s", lib_names[i]);
    }

    printf("Linking: %s\n", cmd);

    // 3. Invoke linker
    int status = system(cmd);
    if (status != 0)
    {
        fprintf(stderr, "Linker failed with exit code %d\n", status);
        return -1;
    }

    // 4. Clean up temp object file
    remove(o_path);

    printf("IRGen: Successfully produced executable %s\n", exe_path);
    return 0;
}

static void
generate_output(c_grammar_node_t const * ast_root, char const * input_filename)
{
    printf("\nStarting LLVM IR Generation...\n");
    ir_generator_ctx_t * ir_ctx = ir_generator_init();
    if (ir_ctx == NULL)
    {
        fprintf(stderr, "Failed to initialize LLVM IR generator.\n");
        return;
    }

    if (ast_root != NULL)
    {
        LLVMModuleRef llvm_module = generate_llvm_ir(ir_ctx, ast_root);
        if (llvm_module)
        {
            if (!compile_only_flag && !assembly_only_flag)
            {
                // Default: link to executable
                char const * exe_path = output_filename ? output_filename : "a.out";
                char temp_o_path[1024];
                snprintf(temp_o_path, sizeof(temp_o_path), "%s.tmp.o", exe_path);

                if (link_to_executable(llvm_module, temp_o_path, exe_path) != 0)
                {
                    fprintf(stderr, "Failed to produce executable %s\n", exe_path);
                }
            }
            else if (assembly_only_flag)
            {
                // -S: emit assembly or IR
                char const * out_path;
                if (output_filename)
                {
                    out_path = output_filename;
                }
                else if (emit_llvm_flag)
                {
                    out_path = derive_output_filename(input_filename, "ll");
                }
                else
                {
                    out_path = derive_output_filename(input_filename, "s");
                }

                if (emit_llvm_flag)
                {
                    // -S -emit-llvm: emit LLVM IR text
                    if (write_llvm_ir_to_file(llvm_module, out_path) != 0)
                    {
                        fprintf(stderr, "Failed to write LLVM IR to %s\n", out_path);
                    }
                }
                else
                {
                    // -S: emit native assembly
                    if (emit_to_file(llvm_module, out_path, march_target, LLVMAssemblyFile) != 0)
                    {
                        fprintf(stderr, "Failed to emit assembly to %s\n", out_path);
                    }
                }
            }
            else if (compile_only_flag)
            {
                // -c: emit object code
                char const * out_path;
                if (output_filename)
                {
                    out_path = output_filename;
                }
                else if (emit_llvm_flag)
                {
                    out_path = derive_output_filename(input_filename, "ll");
                }
                else
                {
                    out_path = derive_output_filename(input_filename, "o");
                }

                if (emit_llvm_flag)
                {
                    // -c -emit-llvm: emit LLVM IR text
                    if (write_llvm_ir_to_file(llvm_module, out_path) != 0)
                    {
                        fprintf(stderr, "Failed to write LLVM IR to %s\n", out_path);
                    }
                }
                else
                {
                    // -c: emit native object file
                    if (emit_to_file(llvm_module, out_path, march_target, LLVMObjectFile) != 0)
                    {
                        fprintf(stderr, "Failed to emit object file %s\n", out_path);
                    }
                }
            }
        }
        else
        {
            fprintf(stderr, "LLVM IR generation failed.\n");
        }
    }
    else
    {
        fprintf(stderr, "AST root is NULL, cannot generate LLVM IR.\n");
    }

    ir_generator_dispose(ir_ctx);
    return;
}

static void
print_usage(char const * prog_name)
{
    fprintf(stderr, "Usage: %s [options] <filename>\n", prog_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -S              Compile to assembly (or LLVM IR with -emit-llvm)\n");
    fprintf(stderr, "  -c              Compile to object code\n");
    fprintf(stderr, "  --emit-llvm     Emit LLVM IR instead of native output\n");
    fprintf(stderr, "  -o <file>       Specify output filename\n");
    fprintf(stderr, "  -l <lib>        Link library (e.g., -lm for libm)\n");
    fprintf(stderr, "  -L <path>       Add library search path\n");
    fprintf(stderr, "  --march=<arch>  Specify target architecture (default: x86-64)\n");
    fprintf(stderr, "  -h, --help      Display this help message\n");
}

int
main(int argc, char * argv[])
{
    static struct option long_options[]
        = {{"march", required_argument, 0, 'm'},
           {"emit-llvm", no_argument, 0, 'e'},
           {"help", no_argument, 0, 'h'},
           {0, 0, 0, 0}};

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "cSo:l:L:h", long_options, &option_index)) != -1)
    {
        switch (opt)
        {
        case 'c':
            compile_only_flag = true;
            break;
        case 'S':
            assembly_only_flag = true;
            break;
        case 'o':
            output_filename = optarg;
            break;
        case 'l':
            if (lib_names_count < (int)(sizeof(lib_names) / sizeof(lib_names[0])))
                lib_names[lib_names_count++] = optarg;
            break;
        case 'L':
            if (lib_paths_count < (int)(sizeof(lib_paths) / sizeof(lib_paths[0])))
                lib_paths[lib_paths_count++] = optarg;
            break;
        case 'm':
            march_target = optarg;
            break;
        case 'e':
            emit_llvm_flag = true;
            break;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (optind >= argc)
    {
        fprintf(stderr, "Error: Missing input filename.\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    char const * filename = argv[optind];
    printf("Attempting to parse C file: %s\n", filename);

    epc_parser_list * list = epc_parser_list_create();
    if (list == NULL)
    {
        fprintf(stderr, "Failed to create parser list.\n");
        return EXIT_FAILURE;
    }

    parse_session_ctx_t * session_ctx = session_ctx_create();
    if (session_ctx == NULL)
    {
        fprintf(stderr, "Failed to create session context.\n");
        epc_parser_list_free(list);
        return EXIT_FAILURE;
    }

    epc_parser_t * c_parser = create_c_grammar_parser(list);
    if (c_parser == NULL)
    {
        fprintf(stderr, "Failed to create C parser.\n");
        session_ctx_free(session_ctx);
        epc_parser_list_free(list);
        return EXIT_FAILURE;
    }

    epc_parse_session_t session = epc_parse_file(c_parser, filename, session_ctx);

    if (session.result.is_error)
    {
        epc_parser_error_t * err = session.result.data.error;
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
    // Print the CPT (commented out for cleaner output)
    // char * cpt_str = epc_cpt_to_string(session.internal_parse_ctx, session.result.data.success);
    // if (cpt_str != NULL)
    // {
    //     printf("Concrete Parse Tree:\n%s\n", cpt_str);
    //     free(cpt_str);
    // }

    // Build the AST
    epc_ast_hook_registry_t * registry = epc_ast_hook_registry_create(C_GRAMMAR_AST_ACTION_COUNT__);
    if (registry != NULL)
    {
        c_grammar_ast_hook_registry_init(registry);
        epc_ast_result_t ast_result = epc_ast_build(session.result.data.success, registry, NULL);

        if (!ast_result.has_error)
        {
            c_grammar_node_t * ast_root = ast_result.ast_root;
            fprintf(stderr, "Starting AST print...\n");
            print_ast(ast_root, 0);
            fprintf(stderr, "Starting LLVM IR Generation...\n");
            generate_output(ast_root, filename);
            c_grammar_node_free(ast_root, NULL);
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
