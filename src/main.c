#include "ast_print.h"
#include "c_grammar.h"
#include "c_grammar_ast.h"
#include "c_grammar_ast_actions.h"
#include "callbacks.h"

// Include for LLVM IR Generator
#include "llvm_ir_generator.h"

#include <easy_pc/easy_pc.h>
#include <errno.h>   // For errno
#include <getopt.h>  // For getopt_long
#include <spawn.h>   // For posix_spawn
#include <stdbool.h> // For bool type
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h> // For waitpid
#include <unistd.h>   // For exec functions

// Declare environ for posix_spawn
extern char ** environ;

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

// Preprocessing options
static bool preprocess_flag = true;       // Default: preprocess enabled
static bool preprocess_only_flag = false; // -E flag: preprocess only
static char * include_paths[64];          // -I flags
static int include_paths_count = 0;
static char * defines[64]; // -D flags
static int defines_count = 0;

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
symbol_table_create(void)
{
    symbol_table_t * st = calloc(1, sizeof(*st));
    if (st == NULL)
        return NULL;

    st->capacity = INITIAL_SYMBOL_CAPACITY;
    st->names = malloc(sizeof(*st->names) * st->capacity);
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
session_ctx_create(void)
{
    parse_session_ctx_t * ctx = calloc(1, sizeof(*ctx));
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
link_to_executable(LLVMModuleRef llvm_module, char const * exe_path)
{
    char o_path[1024];
    snprintf(o_path, sizeof(o_path), "%s.tmp.o", exe_path);

    // 1. Emit object file
    if (emit_to_file(llvm_module, o_path, march_target, LLVMObjectFile) != 0)
    {
        fprintf(stderr, "Failed to emit object file %s\n", o_path);
        return -1;
    }

    // 2. Build argv array for posix_spawn
    // Maximum args: cc, -no-pie, obj, -o, exe, -L paths, -l libs, NULL terminator
    int max_args = 5 + lib_paths_count + lib_names_count + 1;
    char ** argv = calloc(max_args, sizeof(*argv));
    int arg_idx = 0;

    argv[arg_idx++] = strdup("cc");
    argv[arg_idx++] = strdup("-no-pie");
    argv[arg_idx++] = strdup(o_path);

    // Add library search paths
    for (int i = 0; i < lib_paths_count; i++)
    {
        // Allocate space for -L + path
        char * lpath;
        int asres = asprintf(&lpath, "-L%s", lib_paths[i]);
        (void)asres;
        argv[arg_idx++] = lpath;
    }

    // Add output filename
    argv[arg_idx++] = strdup("-o");
    argv[arg_idx++] = strdup(exe_path);

    // Add libraries (must come after -o)
    for (int i = 0; i < lib_names_count; i++)
    {
        // Allocate space for -l + name
        char * lname;
        int asres = asprintf(&lname, "-l%s", lib_names[i]);
        (void)asres;
        argv[arg_idx++] = lname;
    }

    argv[arg_idx++] = NULL;

    // Print command for user feedback
    printf("Linking: cc -no-pie %s", o_path);
    for (int i = 0; i < lib_paths_count; i++)
    {
        printf(" -L%s", lib_paths[i]);
    }
    printf(" -o %s", exe_path);
    for (int i = 0; i < lib_names_count; i++)
    {
        printf(" -l%s", lib_names[i]);
    }
    printf("\n");

    // 3. Invoke linker using posix_spawn
    pid_t pid;
    int result = 0;
    int status = posix_spawn(&pid, "/usr/bin/cc", NULL, NULL, argv, environ);
    if (status != 0)
    {
        fprintf(stderr, "posix_spawn failed: %s\n", strerror(status));
        result = -1;
    }
    else if (waitpid(pid, &status, 0) == -1) // Wait for child process to complete
    {
        fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
        result = -1;
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) // Check exit status
    {
        fprintf(stderr, "Linker failed with exit code %d\n", WEXITSTATUS(status));
        result = -1;
    }
    else if (WIFSIGNALED(status))
    {
        fprintf(stderr, "Linker killed by signal %d\n", WTERMSIG(status));
        result = -1;
    }

    // Free allocated memory for -L and -l arguments
    for (int i = 0; i < arg_idx - 1; i++)
    {
        free(argv[i]);
    }
    free(argv);
    // 4. Clean up temp object file
    remove(o_path);

    if (result == 0)
    {
        printf("IRGen: Successfully produced executable %s\n", exe_path);
    }

    return result;
}

// Preprocess a file using clang -E
static int
preprocess_file(char const * input_path, char const * output_path, bool output_to_stdout)
{
    // Build command: clang -E [includes] [defines] input_file [-o output]
    // Count arguments: clang, -E, [input], [-o, output], [for each -I: -I, path], [for each -D: -D, define], NULL
    int num_args = 4; // clang, -E, input, -o (if not stdout)
    if (!output_to_stdout && output_path)
    {
        num_args += 2; // -o and output file
    }
    // Add space for -I and -D flags (2 args each)
    num_args += (include_paths_count * 2) + (defines_count * 2);
    num_args += 1; // NULL terminator

    char ** argv = calloc(num_args, sizeof(*argv));
    if (!argv)
    {
        fprintf(stderr, "Error: Failed to allocate memory for preprocessing command.\n");
        return -1;
    }
    int arg_idx = 0;

    argv[arg_idx++] = strdup("clang");
    argv[arg_idx++] = strdup("-E");

    // Add -I flags
    for (int i = 0; i < include_paths_count; i++)
    {
        char * path_arg;
        if (asprintf(&path_arg, "-I%s", include_paths[i]) == -1)
        {
            fprintf(stderr, "Error: Failed to create -I argument.\n");
            // Clean up allocated args
            for (int j = 0; j < arg_idx; j++)
                free(argv[j]);
            free(argv);
            return -1;
        }
        argv[arg_idx++] = path_arg;
    }

    // Add -D flags
    for (int i = 0; i < defines_count; i++)
    {
        char * define_arg;
        if (asprintf(&define_arg, "-D%s", defines[i]) == -1)
        {
            fprintf(stderr, "Error: Failed to create -D argument.\n");
            // Clean up allocated args
            for (int j = 0; j < arg_idx; j++)
                free(argv[j]);
            free(argv);
            return -1;
        }
        argv[arg_idx++] = define_arg;
    }

    argv[arg_idx++] = strdup(input_path);

    if (!output_to_stdout && output_path)
    {
        argv[arg_idx++] = strdup("-o");
        argv[arg_idx++] = strdup(output_path);
    }

    argv[arg_idx++] = NULL;

    // Debug: print the command
    fprintf(stderr, "DEBUG: Preprocessing command:");
    for (int i = 0; i < arg_idx; i++)
    {
        fprintf(stderr, " %s", argv[i]);
    }
    fprintf(stderr, "\n");
    // Execute using posix_spawn
    pid_t pid;
    int status = posix_spawn(&pid, "/usr/bin/clang", NULL, NULL, argv, environ);
    if (status != 0)
    {
        fprintf(stderr, "posix_spawn failed: %s\n", strerror(status));
        // Clean up
        for (int i = 0; i < arg_idx; i++)
            free(argv[i]);
        free(argv);
        return -1;
    }

    int wait_status;
    if (waitpid(pid, &wait_status, 0) == -1)
    {
        fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
        // Clean up
        for (int i = 0; i < arg_idx; i++)
            free(argv[i]);
        free(argv);
        return -1;
    }

    // Clean up allocated arguments
    for (int i = 0; i < arg_idx; i++)
        free(argv[i]);
    free(argv);

    if (WIFEXITED(wait_status))
    {
        int exit_code = WEXITSTATUS(wait_status);
        if (exit_code != 0)
        {
            fprintf(stderr, "Preprocessor failed with exit code %d\n", exit_code);
            return -1;
        }
        if (!output_to_stdout)
        {
            printf("Preprocessing: Successfully created %s\n", output_path);
        }
        return 0;
    }
    else if (WIFSIGNALED(wait_status))
    {
        fprintf(stderr, "Preprocessor killed by signal %d\n", WTERMSIG(wait_status));
        return -1;
    }

    return -1; // Should not reach here
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

                if (link_to_executable(llvm_module, exe_path) != 0)
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
    fprintf(stderr, "  -E              Preprocess only, output to stdout\n");
    fprintf(stderr, "  --no-preprocess Skip preprocessing (default behavior before this change)\n");
    fprintf(stderr, "  -o <file>       Specify output filename\n");
    fprintf(stderr, "  -l <lib>        Link library (e.g., -lm for libm)\n");
    fprintf(stderr, "  -L <path>       Add library search path\n");
    fprintf(stderr, "  -I <dir>        Add include directory for preprocessing\n");
    fprintf(stderr, "  -D <macro>      Define macro for preprocessing\n");
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
           {"no-preprocess", no_argument, 0, 256},
           {0, 0, 0, 0}};

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "cSo:l:L:hEI:D:", long_options, &option_index)) != -1)
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
        case 'E':
            preprocess_only_flag = true;
            preprocess_flag = true; // -E implies preprocessing
            break;
        case 'I':
            if (include_paths_count < 64)
                include_paths[include_paths_count++] = optarg;
            break;
        case 'D':
            if (defines_count < 64)
                defines[defines_count++] = optarg;
            break;
        case 256: // --no-preprocess
            preprocess_flag = false;
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

    // Handle preprocessing
    bool should_preprocess = preprocess_flag;
    char const * actual_input_file = filename;
    char * preprocessed_temp_file = NULL;

    if (preprocess_only_flag)
    {
        // -E flag: just preprocess and output to stdout, then exit
        int result = preprocess_file(filename, NULL, true);
        // Cleanup and exit
        return (result == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (should_preprocess)
    {
        // Create temp file for preprocessed output
        preprocessed_temp_file = strdup("/tmp/ncc_preproc_XXXXXX");
        int fd = mkstemp(preprocessed_temp_file);
        if (fd == -1)
        {
            fprintf(stderr, "Error: Failed to create temp file for preprocessing.\n");
            free(preprocessed_temp_file);
            preprocessed_temp_file = NULL;
        }
        else
        {
            close(fd);
        }

        if (preprocessed_temp_file != NULL)
        {
            int prep_result = preprocess_file(filename, preprocessed_temp_file, false);
            if (prep_result != 0)
            {
                fprintf(stderr, "Error: Preprocessing failed for %s\n", filename);
                free(preprocessed_temp_file);
                preprocessed_temp_file = NULL;
            }
            else
            {
                actual_input_file = preprocessed_temp_file;
            }
        }
    }

    epc_parser_list * list = epc_parser_list_create();
    if (list == NULL)
    {
        fprintf(stderr, "Failed to create parser list.\n");
        if (preprocessed_temp_file)
        {
            free(preprocessed_temp_file);
        }
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

    epc_parse_session_t session = epc_parse_file(c_parser, actual_input_file, session_ctx);

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
    //{
    //    printf("Concrete Parse Tree:\n%s\n", cpt_str);
    //    free(cpt_str);
    //}

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
            print_ast(ast_root);
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
