#include "llvm_ir_generator.h"

#include "c_grammar_ast.h" // Assumes this header defines c_grammar_node_t and its node types

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declare the context structure as it's used before its definition
// typedef struct ir_generator_ctx ir_generator_ctx_t; // Assuming this is declared in a header or elsewhere

// Symbol table management functions
static void add_symbol(ir_generator_ctx_t * ctx, char const * name, LLVMValueRef ptr, LLVMTypeRef type);
static LLVMValueRef
get_variable_pointer(ir_generator_ctx_t * ctx, c_grammar_node_t * identifier_node, LLVMTypeRef * out_type);
static void free_symbol_table(ir_generator_ctx_t * ctx);
static bool find_symbol(
    ir_generator_ctx_t * ctx, char const * name, LLVMValueRef * out_ptr, LLVMTypeRef * out_type
); // Helper for get_variable_pointer

// Helper to map C types to LLVM types
static LLVMTypeRef map_type(ir_generator_ctx_t * ctx, c_grammar_node_t const * specifiers, c_grammar_node_t const * declarator);

// --- Forward Declarations ---
// Recursive function to process AST nodes
static void process_ast_node(ir_generator_ctx_t * ctx, c_grammar_node_t const * node);
// Helper function to process expressions and return LLVM ValueRef
static LLVMValueRef process_expression(ir_generator_ctx_t * ctx, c_grammar_node_t * node);
// Placeholder for symbol table operations
// static void add_symbol(ir_generator_ctx_t* ctx, const char* name, LLVMValueRef ptr, LLVMTypeRef type);

/**
 * @brief Adds a symbol to the current scope's symbol table.
 * @param ctx The IR generator context.
 * @param name The name of the symbol.
 * @param ptr The LLVMValueRef pointer to the symbol's memory.
 * @param type The LLVMTypeRef of the symbol.
 */
static void
add_symbol(ir_generator_ctx_t * ctx, char const * name, LLVMValueRef ptr, LLVMTypeRef type)
{
    if (!ctx || !name || !ptr || !type)
    {
        fprintf(stderr, "IRGen: Invalid arguments for add_symbol.\n");
        return;
    }

    // Check if symbol table needs resizing
    if (ctx->symbol_count >= ctx->symbol_capacity)
    {
        size_t new_capacity = ctx->symbol_capacity == 0 ? 16 : ctx->symbol_capacity * 2;
        // Reallocate space for the symbol table
        symbol_t * new_table = realloc(ctx->symbol_table, new_capacity * sizeof(symbol_t));
        if (!new_table)
        {
            fprintf(stderr, "IRGen: Failed to resize symbol table.\n");
            // Handle error: could return an error code or exit. For now, just report.
            return;
        }
        ctx->symbol_table = new_table;
        ctx->symbol_capacity = new_capacity;
    }

    // Store the symbol
    ctx->symbol_table[ctx->symbol_count].name = strdup(name); // Duplicate name to own it
    if (!ctx->symbol_table[ctx->symbol_count].name)
    {
        fprintf(stderr, "IRGen: Failed to duplicate symbol name string '%s'.\n", name);
        // Clean up partially allocated entry if necessary
        return;
    }
    ctx->symbol_table[ctx->symbol_count].ptr = ptr;
    ctx->symbol_table[ctx->symbol_count].type = type;
    ctx->symbol_count++;

    fprintf(stderr, "IRGen: Added symbol '%s' to table.\n", name);
}

/**
 * @brief Finds a symbol in the symbol table and returns its pointer and type.
 * @param ctx The IR generator context.
 * @param name The name of the symbol to find.
 * @param out_ptr Pointer to store the found LLVMValueRef.
 * @param out_type Pointer to store the found LLVMTypeRef.
 * @return True if the symbol was found, false otherwise.
 */
static bool
find_symbol(ir_generator_ctx_t * ctx, char const * name, LLVMValueRef * out_ptr, LLVMTypeRef * out_type)
{
    if (!ctx || !name || !ctx->symbol_table)
    {
        return false;
    }

    // Search for the symbol by name
    for (size_t i = 0; i < ctx->symbol_count; ++i)
    {
        if (ctx->symbol_table[i].name && strcmp(ctx->symbol_table[i].name, name) == 0)
        {
            if (out_ptr)
                *out_ptr = ctx->symbol_table[i].ptr;
            if (out_type)
                *out_type = ctx->symbol_table[i].type;
            return true;
        }
    }
    return false;
}

static void register_struct_definition(ir_generator_ctx_t * ctx, c_grammar_node_t * type_child);
static void add_struct_type(ir_generator_ctx_t * ctx, char const * name, LLVMTypeRef struct_type, char **member_names, LLVMTypeRef *member_types, size_t num_members);
static LLVMTypeRef find_struct_type(ir_generator_ctx_t * ctx, char const * name);
static struct_info_t *find_struct_info(ir_generator_ctx_t * ctx, char const * name);
static c_grammar_node_t *find_direct_declarator(c_grammar_node_t * declarator);

static void
register_structs_in_node(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (!ctx || !node)
        return;
    
    bool is_struct_declaration = false;
    
    if (node->type == AST_NODE_DECLARATION)
    {
        for (size_t i = 0; i < node->data.list.count; ++i)
        {
            c_grammar_node_t * child = node->data.list.children[i];
            if (!child) continue;
            if (child->type == AST_NODE_DECL_SPECIFIERS && !child->is_terminal_node)
            {
                for (size_t j = 0; j < child->data.list.count; ++j)
                {
                    c_grammar_node_t * spec_child = child->data.list.children[j];
                    if (spec_child && spec_child->type == AST_NODE_TYPE_SPECIFIER && !spec_child->is_terminal_node)
                    {
                        for (size_t k = 0; k < spec_child->data.list.count; ++k)
                        {
                            c_grammar_node_t * type_child = spec_child->data.list.children[k];
                            if (type_child && type_child->type == AST_NODE_STRUCT_DEFINITION)
                            {
                                register_struct_definition(ctx, type_child);
                                is_struct_declaration = true;
                            }
                        }
                    }
                }
            }
        }
    }
    
    if (!node->is_terminal_node && node->data.list.children && !is_struct_declaration)
    {
        for (size_t i = 0; i < node->data.list.count; ++i)
        {
            register_structs_in_node(ctx, node->data.list.children[i]);
        }
    }
}

static void
register_struct_definition(ir_generator_ctx_t * ctx, c_grammar_node_t * type_child)
{
    if (!ctx || !type_child || type_child->type != AST_NODE_STRUCT_DEFINITION)
        return;
    
    char * struct_name = NULL;
    LLVMTypeRef * member_types = NULL;
    char ** member_names = NULL;
    size_t num_members = 0;
    
    for (size_t m = 0; m < type_child->data.list.count; ++m)
    {
        c_grammar_node_t * struct_child = type_child->data.list.children[m];
        if (struct_child && struct_child->type == AST_NODE_IDENTIFIER && struct_child->is_terminal_node)
        {
            struct_name = struct_child->data.terminal.text;
            break;
        }
    }
    
    if (!struct_name)
        return;
    
    if (find_struct_type(ctx, struct_name))
        return;
    
    for (size_t m = 1; m + 1 < type_child->data.list.count; m += 2)
    {
        c_grammar_node_t * type_spec = type_child->data.list.children[m];
        c_grammar_node_t * decl = type_child->data.list.children[m + 1];
        if (type_spec && decl && 
            type_spec->type == AST_NODE_TYPE_SPECIFIER && 
            decl->type == AST_NODE_DECLARATOR)
        {
            // Get member type
            LLVMTypeRef member_type = map_type(ctx, type_spec, decl);
            
            // Get member name from declarator
            char * member_name = NULL;
            c_grammar_node_t * direct_decl = find_direct_declarator(decl);
            if (direct_decl && direct_decl->data.list.count > 0)
            {
                c_grammar_node_t * ident = direct_decl->data.list.children[0];
                if (ident && ident->type == AST_NODE_IDENTIFIER && ident->is_terminal_node)
                {
                    member_name = ident->data.terminal.text;
                }
            }
            
            LLVMTypeRef * new_types = realloc(member_types, (num_members + 1) * sizeof(LLVMTypeRef));
            char ** new_names = realloc(member_names, (num_members + 1) * sizeof(char *));
            if (new_types && new_names)
            {
                member_types = new_types;
                member_names = new_names;
                member_types[num_members] = member_type;
                member_names[num_members] = member_name ? strdup(member_name) : NULL;
                num_members++;
            }
        }
    }
    
    if (struct_name && num_members > 0 && member_types)
    {
        LLVMTypeRef struct_type = LLVMStructCreateNamed(ctx->context, struct_name);
        LLVMStructSetBody(struct_type, member_types, (unsigned)num_members, false);
        add_struct_type(ctx, struct_name, struct_type, member_names, member_types, num_members);
    }
    
    if (member_types)
        free(member_types);
    if (member_names)
    {
        for (size_t i = 0; i < num_members; i++)
            if (member_names[i]) free(member_names[i]);
        free(member_names);
    }
}

static struct_info_t *
find_struct_info(ir_generator_ctx_t * ctx, char const * name)
{
    if (!ctx || !name)
        return NULL;

    for (size_t i = 0; i < ctx->struct_count; ++i)
    {
        if (ctx->structs[i].name && strcmp(ctx->structs[i].name, name) == 0)
        {
            return &ctx->structs[i];
        }
    }
    return NULL;
}

static LLVMTypeRef
find_struct_type(ir_generator_ctx_t * ctx, char const * name)
{
    struct_info_t *info = find_struct_info(ctx, name);
    return info ? info->type : NULL;
}

static void
add_struct_type(ir_generator_ctx_t * ctx, char const * name, LLVMTypeRef struct_type, char **member_names, LLVMTypeRef *member_types, size_t num_members)
{
    if (!ctx || !name || !struct_type)
        return;

    // Check if already exists
    if (find_struct_type(ctx, name))
        return;

    // Resize if needed
    if (ctx->struct_count >= ctx->struct_capacity)
    {
        size_t new_capacity = ctx->struct_capacity * 2;
        struct_info_t *new_structs = realloc(ctx->structs, new_capacity * sizeof(struct_info_t));
        if (!new_structs)
            return;
        ctx->structs = new_structs;
        ctx->struct_capacity = new_capacity;
    }

    ctx->structs[ctx->struct_count].name = strdup(name);
    ctx->structs[ctx->struct_count].type = struct_type;
    ctx->structs[ctx->struct_count].field_count = num_members;
    ctx->structs[ctx->struct_count].fields = NULL;
    
    if (num_members > 0 && member_names && member_types)
    {
        ctx->structs[ctx->struct_count].fields = calloc(num_members, sizeof(struct_field_t));
        for (size_t i = 0; i < num_members; i++)
        {
            ctx->structs[ctx->struct_count].fields[i].name = strdup(member_names[i]);
            ctx->structs[ctx->struct_count].fields[i].type = member_types[i];
        }
    }
    
    ctx->struct_count++;
}

static char *
decode_string(char const * const src)
{
    if (src == NULL)
    {
        return NULL;
    }

    size_t const len = strlen(src);
    char * const decoded = malloc(len + 1);
    if (decoded == NULL)
    {
        return NULL;
    }

    size_t i = 0;
    size_t j = 0;
    while (i < len)
    {
        if (src[i] == '\\' && i + 1 < len)
        {
            switch (src[i + 1])
            {
                case 'n': decoded[j++] = '\n'; break;
                case 't': decoded[j++] = '\t'; break;
                case 'r': decoded[j++] = '\r'; break;
                case '0': decoded[j++] = '\0'; break;
                case '\\': decoded[j++] = '\\'; break;
                case '\"': decoded[j++] = '\"'; break;
                case '\'': decoded[j++] = '\''; break;
                default: decoded[j++] = src[i + 1]; break;
            }
            i += 2;
        }
        else
        {
            decoded[j++] = src[i++];
        }
    }
    decoded[j] = '\0';
    return decoded;
}

// --- IR Generator Context Initialization and Disposal ---
static c_grammar_node_t *
find_direct_declarator(c_grammar_node_t * declarator)
{
    if (!declarator || declarator->type != AST_NODE_DECLARATOR)
        return NULL;

    for (size_t i = 0; i < declarator->data.list.count; ++i)
    {
        if (declarator->data.list.children[i]->type == AST_NODE_DIRECT_DECLARATOR)
        {
            return declarator->data.list.children[i];
        }
    }
    return NULL;
}

static LLVMTypeRef
map_type(ir_generator_ctx_t * ctx, c_grammar_node_t const * specifiers, c_grammar_node_t const * declarator)
{
    LLVMTypeRef base_type = NULL;
    int pointer_level = 0;
    size_t * array_sizes = NULL;
    size_t array_depth = 0;
    size_t array_capacity = 4;

    array_sizes = malloc(array_capacity * sizeof(size_t));
    if (!array_sizes)
    {
        return LLVMInt32TypeInContext(ctx->context);
    }

    // 1. Process Specifiers (extract base type and any pointers in specifiers)
    if (specifiers)
    {
        if (specifiers->type == AST_NODE_TYPE_SPECIFIER)
        {
            if (specifiers->is_terminal_node)
            {
                char const * type_name = specifiers->data.terminal.text;
                if (strncmp(type_name, "int", 3) == 0) base_type = LLVMInt32TypeInContext(ctx->context);
                else if (strncmp(type_name, "char", 4) == 0) base_type = LLVMInt8TypeInContext(ctx->context);
                else if (strncmp(type_name, "void", 4) == 0) base_type = LLVMVoidTypeInContext(ctx->context);
                else if (strncmp(type_name, "float", 5) == 0) base_type = LLVMFloatTypeInContext(ctx->context);
                else if (strncmp(type_name, "double", 6) == 0) base_type = LLVMDoubleTypeInContext(ctx->context);
                else if (strncmp(type_name, "long", 4) == 0) base_type = LLVMInt64TypeInContext(ctx->context);
                else if (strncmp(type_name, "short", 5) == 0) base_type = LLVMInt16TypeInContext(ctx->context);
            }
            else
            {
                c_grammar_node_t * type_specifier_node = specifiers;
                while (type_specifier_node && type_specifier_node->type == AST_NODE_TYPE_SPECIFIER && !type_specifier_node->is_terminal_node)
                {
                    if (type_specifier_node->data.list.count == 1 && type_specifier_node->data.list.children[0]->type == AST_NODE_TYPE_SPECIFIER)
                    {
                        type_specifier_node = type_specifier_node->data.list.children[0];
                    }
                    else
                    {
                        break;
                    }
                }
                if (type_specifier_node && !type_specifier_node->is_terminal_node && type_specifier_node->data.list.count > 0)
                {
                    for (size_t i = 0; i < type_specifier_node->data.list.count; ++i)
                    {
                        c_grammar_node_t * child = type_specifier_node->data.list.children[i];
                        if (child && child->is_terminal_node && child->type == AST_NODE_IDENTIFIER)
                        {
                            char const * type_name = child->data.terminal.text;
                            // Check if it's a registered struct type first
                            LLVMTypeRef struct_type = find_struct_type(ctx, type_name);
                            if (struct_type)
                            {
                                base_type = struct_type;
                            }
                            else if (strncmp(type_name, "int", 3) == 0) base_type = LLVMInt32TypeInContext(ctx->context);
                            else if (strncmp(type_name, "char", 4) == 0) base_type = LLVMInt8TypeInContext(ctx->context);
                            else if (strncmp(type_name, "void", 4) == 0) base_type = LLVMVoidTypeInContext(ctx->context);
                            else if (strncmp(type_name, "float", 5) == 0) base_type = LLVMFloatTypeInContext(ctx->context);
                            else if (strncmp(type_name, "double", 6) == 0) base_type = LLVMDoubleTypeInContext(ctx->context);
                            else if (strncmp(type_name, "long", 4) == 0) base_type = LLVMInt64TypeInContext(ctx->context);
                            else if (strncmp(type_name, "short", 5) == 0) base_type = LLVMInt16TypeInContext(ctx->context);
                        }
                    }
                }
            }
        }
        else if (specifiers->type == AST_NODE_DECL_SPECIFIERS)
        {
            for (size_t i = 0; i < specifiers->data.list.count; ++i)
            {
                c_grammar_node_t * child = specifiers->data.list.children[i];
                if (child->type == AST_NODE_TYPE_SPECIFIER)
                {
                    base_type = map_type(ctx, child, NULL);
                }
                else if (child->type == AST_NODE_POINTER)
                {
                    pointer_level++;
                }
            }
        }
    }

    // 2. Process Declarator (extract pointers and arrays)
    if (declarator && declarator->type == AST_NODE_DECLARATOR)
    {
        for (size_t i = 0; i < declarator->data.list.count; ++i)
        {
            c_grammar_node_t * child = declarator->data.list.children[i];
            if (child->type == AST_NODE_POINTER)
            {
                pointer_level++;
            }
            else if (child->type == AST_NODE_DECLARATOR_SUFFIX)
            {
                // Check for array suffixes in declaration
                // DeclaratorSuffix structure: contains LBracket, Expression, RBracket
                // But the Expression is directly in the suffix for declarations
                for (size_t j = 0; j < child->data.list.count; ++j)
                {
                    c_grammar_node_t * suffix_child = child->data.list.children[j];
                    // For array declarations like int arr[5], the suffix contains IntegerValue directly
                    if (suffix_child && !suffix_child->is_terminal_node)
                    {
                        if (suffix_child->type == AST_NODE_INTEGER_VALUE)
                        {
                            c_grammar_node_t * base_node = suffix_child->data.list.children[0];
                            if (base_node && base_node->is_terminal_node)
                            {
                                unsigned long long size_val = strtoull(base_node->data.terminal.text, NULL, 10);
                                if (array_depth < array_capacity)
                                {
                                    array_sizes[array_depth] = (size_t)size_val;
                                    array_depth++;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (!base_type)
    {
        base_type = LLVMInt32TypeInContext(ctx->context);
    }

    // Build array types from innermost to outermost
    LLVMTypeRef final_type = base_type;
    for (int i = (int)array_depth - 1; i >= 0; --i)
    {
        final_type = LLVMArrayType(final_type, (unsigned)array_sizes[i]);
    }

    // Add pointer types
    for (int i = 0; i < pointer_level; ++i)
    {
        final_type = LLVMPointerType(final_type, 0);
    }

    if (array_sizes)
        free(array_sizes);

    return final_type;
}

/**
 * @brief Initializes the IR generator context.
 * Creates LLVM context, module, and builder.
 */
ir_generator_ctx_t *
ir_generator_init()
{
    ir_generator_ctx_t * ctx = calloc(1, sizeof(ir_generator_ctx_t));
    if (!ctx)
    {
        fprintf(stderr, "IRGen: Failed to allocate memory for context.\n");
        return NULL;
    }

    ctx->context = LLVMContextCreate();
    if (!ctx->context)
    {
        fprintf(stderr, "IRGen: Failed to create LLVM context.\n");
        free(ctx);
        return NULL;
    }

    ctx->module = LLVMModuleCreateWithName("c_compiler_module");
    if (!ctx->module)
    {
        fprintf(stderr, "IRGen: Failed to create LLVM module.\n");
        LLVMContextDispose(ctx->context);
        free(ctx);
        return NULL;
    }

    ctx->builder = LLVMCreateBuilder();
    if (!ctx->builder)
    {
        fprintf(stderr, "IRGen: Failed to create LLVM builder.\n");
        LLVMDisposeModule(ctx->module);
        LLVMContextDispose(ctx->context);
        free(ctx);
        return NULL;
    }

    // Initialize symbol table
    ctx->symbol_capacity = 16; // Initial capacity
    ctx->symbol_table = malloc(ctx->symbol_capacity * sizeof(symbol_t));
    if (!ctx->symbol_table)
    {
        fprintf(stderr, "IRGen: Failed to allocate memory for symbol table.\n");
        LLVMDisposeModule(ctx->module);
        LLVMContextDispose(ctx->context);
        free(ctx);
        return NULL;
    }
    ctx->symbol_count = 0;

    // Initialize struct type registry
    ctx->struct_capacity = 16;
    ctx->structs = calloc(ctx->struct_capacity, sizeof(struct_info_t));
    ctx->struct_count = 0;

    return ctx;
}

/**
 * @brief Frees the symbol table memory.
 */
static void
free_symbol_table(ir_generator_ctx_t * ctx)
{
    if (!ctx || !ctx->symbol_table)
        return;

    for (size_t i = 0; i < ctx->symbol_count; ++i)
    {
        free(ctx->symbol_table[i].name); // Free allocated name strings
    }
    free(ctx->symbol_table);
    ctx->symbol_table = NULL;
    ctx->symbol_count = 0;
    ctx->symbol_capacity = 0;
}

/**
 * @brief Disposes of the IR generator context and associated LLVM resources.
 */
void
ir_generator_dispose(ir_generator_ctx_t * ctx)
{
    if (!ctx)
        return;

    free_symbol_table(ctx); // Free symbol table first

    // Free struct type registry
    if (ctx->structs)
    {
        for (size_t i = 0; i < ctx->struct_count; ++i)
        {
            if (ctx->structs[i].name)
                free(ctx->structs[i].name);
            if (ctx->structs[i].fields)
            {
                for (size_t j = 0; j < ctx->structs[i].field_count; j++)
                {
                    if (ctx->structs[i].fields[j].name)
                        free(ctx->structs[i].fields[j].name);
                }
                free(ctx->structs[i].fields);
            }
        }
        free(ctx->structs);
    }

    if (ctx->builder)
        LLVMDisposeBuilder(ctx->builder);
    // LLVMDisposeModule takes ownership of the module.
    if (ctx->module)
        LLVMDisposeModule(ctx->module);
    if (ctx->context)
        LLVMContextDispose(ctx->context);

    free(ctx);
}

// --- Main LLVM IR Generation Function ---

/**
 * @brief Generates LLVM IR from the provided AST root.
 * @param ctx The IR generator context.
 * @param ast_root The root node of the AST.
 * @return The LLVM module containing the generated IR, or NULL on failure.
 */
LLVMModuleRef
generate_llvm_ir(ir_generator_ctx_t * ctx, c_grammar_node_t const * ast_root)
{
    if (!ctx || !ast_root)
    {
        fprintf(stderr, "IRGen: Invalid context or AST root provided.\n");
        return NULL;
    }

    register_structs_in_node(ctx, ast_root);

    process_ast_node(ctx, ast_root);

    return ctx->module;
}

// --- AST Node Processing Logic ---

/**
 * @brief Recursively processes AST nodes to generate LLVM IR.
 * This function dispatches to specific handlers based on the node type.
 */
static void
process_ast_node(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (node == NULL)
    {
        return;
    }

    fprintf(stderr, "%s node type %u\n", __func__, node->type);

    switch (node->type)
    {
    case AST_NODE_TRANSLATION_UNIT:
    {
        // Process top-level declarations and function definitions.
        if (node->data.list.children)
        {
            for (size_t i = 0; i < node->data.list.count; ++i)
            {
                process_ast_node(ctx, node->data.list.children[i]);
            }
        }
        break;
    }
    case AST_NODE_FUNCTION_DEFINITION:
    {
        // --- Handle Function Definition ---
        c_grammar_node_t * decl_specifiers_node = NULL;
        c_grammar_node_t * declarator_node = NULL;
        c_grammar_node_t * compound_stmt_node = NULL;

        // Iterate through children to find relevant parts: Declarator, CompoundStatement.
        if (node->data.list.children)
        {
            for (size_t i = 0; i < node->data.list.count; ++i)
            {
                c_grammar_node_t * child = node->data.list.children[i];
                if (child->type == AST_NODE_DECL_SPECIFIERS)
                {
                    decl_specifiers_node = child;
                }
                else if (child->type == AST_NODE_DECLARATOR)
                {
                    declarator_node = child;
                }
                else if (child->type == AST_NODE_COMPOUND_STATEMENT)
                {
                    compound_stmt_node = child;
                }
            }
        }

        if (!declarator_node || !compound_stmt_node)
        {
            fprintf(stderr, "IRGen Error: Incomplete function definition.\n");
            return;
        }

        // --- Extract Function Name ---
        char * func_name = "unknown_function";
        c_grammar_node_t * suffix_node = NULL;

        c_grammar_node_t * direct_decl = find_direct_declarator(declarator_node);
        if (direct_decl && direct_decl->data.list.count > 0 && direct_decl->data.list.children[0]->type == AST_NODE_IDENTIFIER)
        {
            func_name = direct_decl->data.list.children[0]->data.terminal.text;
        }

        // Find parameter suffix
        for (size_t i = 0; i < declarator_node->data.list.count; ++i)
        {
            if (declarator_node->data.list.children[i]->type == AST_NODE_DECLARATOR_SUFFIX)
            {
                suffix_node = declarator_node->data.list.children[i];
                break;
            }
        }

        // --- Extract Parameters ---
        size_t num_params = 0;
        LLVMTypeRef * param_types = NULL;
        char ** param_names = NULL;

        if (suffix_node && suffix_node->data.list.count > 0)
        {
            // Each parameter typically has [TypeSpecifier, Declarator]
            num_params = suffix_node->data.list.count / 2;
            param_types = calloc(num_params, sizeof(LLVMTypeRef));
            param_names = calloc(num_params, sizeof(char *));

            for (size_t i = 0; i < num_params; ++i)
            {
                c_grammar_node_t * p_spec = suffix_node->data.list.children[i * 2];
                c_grammar_node_t * p_decl = suffix_node->data.list.children[i * 2 + 1];

                param_types[i] = map_type(ctx, p_spec, p_decl);

                c_grammar_node_t * p_direct = find_direct_declarator(p_decl);
                if (p_direct && p_direct->data.list.count > 0 && p_direct->data.list.children[0]->type == AST_NODE_IDENTIFIER)
                {
                    param_names[i] = p_direct->data.list.children[0]->data.terminal.text;
                }
            }
        }

        LLVMTypeRef return_type = map_type(ctx, decl_specifiers_node, NULL);
        LLVMTypeRef func_type = LLVMFunctionType(return_type, param_types, (unsigned)num_params, false);
        LLVMValueRef func = LLVMAddFunction(ctx->module, func_name, func_type);

        // Create a basic block for the function's entry point.
        LLVMBasicBlockRef entry_block = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
        LLVMPositionBuilderAtEnd(ctx->builder, entry_block);

        // --- Handle function parameters: allocate space and store arguments ---
        for (size_t i = 0; i < num_params; ++i)
        {
            LLVMValueRef param_val = LLVMGetParam(func, (unsigned)i);
            LLVMValueRef alloca_inst = LLVMBuildAlloca(ctx->builder, param_types[i], param_names[i] ? param_names[i] : "");
            LLVMBuildStore(ctx->builder, param_val, alloca_inst);
            if (param_names[i])
            {
                add_symbol(ctx, param_names[i], alloca_inst, param_types[i]);
            }
        }

        if (param_types)
            free(param_types);
        if (param_names)
            free(param_names);

        // Process the compound statement (function body).
        process_ast_node(ctx, compound_stmt_node);

        // --- Add a default return if the function doesn't end with one ---
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
        {
            if (LLVMGetTypeKind(return_type) == LLVMVoidTypeKind)
            {
                LLVMBuildRetVoid(ctx->builder);
            }
            else
            {
                LLVMBuildRet(ctx->builder, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false));
            }
        }

        break;
    }
    case AST_NODE_COMPOUND_STATEMENT:
    {
        // Process statements within a block.
        // TODO: Implement scope management for symbol table.
        if (node->data.list.children)
        {
            for (size_t i = 0; i < node->data.list.count; ++i)
            {
                process_ast_node(ctx, node->data.list.children[i]);
            }
        }
        break;
    }
    case AST_NODE_EXPRESSION_STATEMENT:
    {
        if (node->data.list.children)
        {
            for (size_t i = 0; i < node->data.list.count; ++i)
            {
                process_expression(ctx, node->data.list.children[i]);
            }
        }
        break;
    }
    case AST_NODE_DECLARATION:
    {
        // --- Handle Variable Declarations ---

        // First, check if this is a struct definition - this is now handled in register_structs_in_node

        c_grammar_node_t * decl_specifiers = NULL;
        for (size_t i = 0; i < node->data.list.count; ++i)
        {
            if (node->data.list.children[i]->type == AST_NODE_DECL_SPECIFIERS)
            {
                decl_specifiers = node->data.list.children[i];
                break;
            }
        }

        // Process InitDeclarators to create variables and initialize them.
        for (size_t i = 0; i < node->data.list.count; ++i)
        {
            if (node->data.list.children[i]->type == AST_NODE_INIT_DECLARATOR)
            {
                c_grammar_node_t * init_decl_node = node->data.list.children[i];

                char * var_name = NULL;
                c_grammar_node_t * initializer_expr_node = NULL; // Node representing the initializer expression.
                c_grammar_node_t * declarator_node = NULL;

                // Extraction of variable name from declarator.
                if (init_decl_node->data.list.count > 0
                    && init_decl_node->data.list.children[0]->type == AST_NODE_DECLARATOR)
                {
                    declarator_node = init_decl_node->data.list.children[0];
                    c_grammar_node_t * direct_decl_node = find_direct_declarator(declarator_node);
                    if (direct_decl_node && direct_decl_node->data.list.count > 0
                        && direct_decl_node->data.list.children[0]->type == AST_NODE_IDENTIFIER)
                    {
                        var_name = direct_decl_node->data.list.children[0]->data.terminal.text;
                    }
                }

                LLVMTypeRef var_type = map_type(ctx, decl_specifiers, declarator_node);

                // Find initializer if it exists (e.g., 'int i = 42;').
                if (init_decl_node->data.list.count > 1)
                {
                    initializer_expr_node = init_decl_node->data.list.children[1];
                }

                if (var_name)
                {
                    // Allocate space for the variable on the stack.
                    LLVMValueRef alloca_inst = LLVMBuildAlloca(ctx->builder, var_type, var_name);

                    // Add this variable to the symbol table.
                    add_symbol(ctx, var_name, alloca_inst, var_type);

                    // If there's an initializer, process it and store the value.
                    if (initializer_expr_node)
                    {
                        LLVMValueRef initializer_value = process_expression(ctx, initializer_expr_node);
                        if (initializer_value)
                        {
                            LLVMTypeRef init_type = LLVMTypeOf(initializer_value);
                            if (LLVMGetTypeKind(var_type) == LLVMFloatTypeKind
                                || LLVMGetTypeKind(var_type) == LLVMDoubleTypeKind)
                            {
                                if (LLVMGetTypeKind(init_type) == LLVMIntegerTypeKind)
                                {
                                    initializer_value =
                                        LLVMBuildSIToFP(ctx->builder, initializer_value, var_type, "casttmp");
                                }
                                else if (LLVMGetTypeKind(init_type) == LLVMFloatTypeKind
                                         && LLVMGetTypeKind(var_type) == LLVMDoubleTypeKind)
                                {
                                    initializer_value =
                                        LLVMBuildFPExt(ctx->builder, initializer_value, var_type, "casttmp");
                                }
                                else if (LLVMGetTypeKind(init_type) == LLVMDoubleTypeKind
                                         && LLVMGetTypeKind(var_type) == LLVMFloatTypeKind)
                                {
                                    initializer_value =
                                        LLVMBuildFPTrunc(ctx->builder, initializer_value, var_type, "casttmp");
                                }
                            }

                            LLVMBuildStore(ctx->builder, initializer_value, alloca_inst);
                        }
                    }
                }
            }
        }
        break;
    }
    case AST_NODE_INIT_DECLARATOR:
    {
        // This node is generally processed within AST_NODE_DECLARATION for simplicity.
        // If the AST structure required it, we would handle it here.
        if (node->data.list.children)
        {
            for (size_t i = 0; i < node->data.list.count; ++i)
            {
                process_ast_node(ctx, node->data.list.children[i]);
            }
        }
        break;
    }
    case AST_NODE_ASSIGNMENT:
    {
        // Handle assignment like 'variable = expression' or 'arr[i] = expression'.
        // Children typically: [LHS_node, Operator_node, RHS_node].
        if (node->data.list.count >= 3 && node->data.list.children)
        {
            c_grammar_node_t * lhs_node = node->data.list.children[0];
            // Operator node (e.g., '=') is skipped for simplicity.
            c_grammar_node_t * rhs_node = node->data.list.children[2];

            LLVMValueRef lhs_ptr = NULL;
            LLVMTypeRef lhs_type = NULL;

            // Check if LHS is a PostfixExpression with array subscript
            if (lhs_node->type == AST_NODE_POSTFIX_EXPRESSION && lhs_node->data.list.count >= 2)
            {
                c_grammar_node_t * base_node = lhs_node->data.list.children[0];
                if (base_node->type == AST_NODE_IDENTIFIER)
                {
                    char const * arr_name = base_node->data.terminal.text;
                    LLVMValueRef arr_ptr;
                    LLVMTypeRef arr_type;
                    if (find_symbol(ctx, arr_name, &arr_ptr, &arr_type))
                    {
                        // Find the array subscript suffix
                        for (size_t i = 1; i < lhs_node->data.list.count; ++i)
                        {
                            c_grammar_node_t * suffix = lhs_node->data.list.children[i];
                            if (suffix->type == AST_NODE_ARRAY_SUBSCRIPT)
                            {
                                // Find the index expression
                                LLVMValueRef index_val = NULL;
                                for (size_t k = 0; k < suffix->data.list.count; ++k)
                                {
                                    c_grammar_node_t * child = suffix->data.list.children[k];
                                    if (child && !child->is_terminal_node)
                                    {
                                        index_val = process_expression(ctx, child);
                                        break;
                                    }
                                }

                                if (index_val)
                                {
                                    // Create GEP to get element pointer
                                    LLVMValueRef indices[2];
                                    indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                                    indices[1] = index_val;
                                    lhs_ptr = LLVMBuildInBoundsGEP2(ctx->builder, arr_type, arr_ptr, indices, 2, "arrayidx");
                                    lhs_type = LLVMGetElementType(arr_type);
                                }
                                break;
                            }
                        }
                    }
                }
            }
            else
            {
                // Simple variable assignment
                lhs_ptr = get_variable_pointer(ctx, lhs_node, &lhs_type);
            }

            if (!lhs_ptr)
            {
                fprintf(stderr, "IRGen Error: Could not get pointer for LHS in assignment.\n");
                return;
            }

            // Process the RHS expression to get its LLVM ValueRef.
            LLVMValueRef rhs_value = process_expression(ctx, rhs_node);
            if (!rhs_value)
            {
                fprintf(stderr, "IRGen Error: Failed to process RHS expression in assignment.\n");
                return;
            }

            // Generate the store instruction.
            LLVMBuildStore(ctx->builder, rhs_value, lhs_ptr);
        }
        break;
    }
    case AST_NODE_FOR_STATEMENT:
    {
        // AST structure for ForStatement: [InitExpr/Decl, CondExpr, PostExpr, BodyStatement]
        if (node->data.list.count < 4)
        {
            fprintf(stderr, "IRGen Error: Invalid ForStatement AST node.\n");
            return;
        }

        c_grammar_node_t * init_node = node->data.list.children[0];
        c_grammar_node_t * cond_node = node->data.list.children[1];
        c_grammar_node_t * post_node = node->data.list.children[2];
        c_grammar_node_t * body_node = node->data.list.children[3];

        LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

        LLVMBasicBlockRef cond_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "for_cond");
        LLVMBasicBlockRef body_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "for_body");
        LLVMBasicBlockRef post_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "for_post");
        LLVMBasicBlockRef after_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "for_after");

        // 1. Process Init
        process_ast_node(ctx, init_node);
        LLVMBuildBr(ctx->builder, cond_block);

        // 2. Emit Cond block
        LLVMPositionBuilderAtEnd(ctx->builder, cond_block);
        LLVMValueRef cond_val = process_expression(ctx, cond_node);
        if (cond_val)
        {
            // Convert condition to bool (i1) if it's not already.
            LLVMTypeRef cond_type = LLVMTypeOf(cond_val);
            if (cond_type != LLVMInt1TypeInContext(ctx->context))
            {
                LLVMValueRef zero = LLVMConstNull(cond_type);
                cond_val = LLVMBuildICmp(ctx->builder, LLVMIntNE, cond_val, zero, "for_cond_bool");
            }
            LLVMBuildCondBr(ctx->builder, cond_val, body_block, after_block);
        }
        else
        {
            // Empty condition is always true
            LLVMBuildBr(ctx->builder, body_block);
        }

        // 3. Emit Body block
        LLVMPositionBuilderAtEnd(ctx->builder, body_block);
        process_ast_node(ctx, body_node);
        // If body doesn't have terminator, jump to post
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
        {
            LLVMBuildBr(ctx->builder, post_block);
        }

        // 4. Emit Post block
        LLVMPositionBuilderAtEnd(ctx->builder, post_block);
        process_expression(ctx, post_node);
        LLVMBuildBr(ctx->builder, cond_block);

        // 5. Continue from after block
        LLVMPositionBuilderAtEnd(ctx->builder, after_block);
        break;
    }
    case AST_NODE_WHILE_STATEMENT:
    {
        // AST structure for WhileStatement: [ConditionExpression, BodyStatement]
        if (node->data.list.count < 2)
        {
            fprintf(stderr, "IRGen Error: Invalid WhileStatement AST node.\n");
            return;
        }

        c_grammar_node_t * condition_node = node->data.list.children[0];
        c_grammar_node_t * body_node = node->data.list.children[1];

        LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

        LLVMBasicBlockRef cond_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "while_cond");
        LLVMBasicBlockRef body_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "while_body");
        LLVMBasicBlockRef after_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "while_after");

        // Jump to condition block
        LLVMBuildBr(ctx->builder, cond_block);

        // --- Emit condition block ---
        LLVMPositionBuilderAtEnd(ctx->builder, cond_block);
        LLVMValueRef condition_val = process_expression(ctx, condition_node);
        if (!condition_val)
        {
            fprintf(stderr, "IRGen Error: Failed to process condition for WhileStatement.\n");
            return;
        }

        // Convert condition to bool (i1) if it's not already.
        LLVMTypeRef cond_type = LLVMTypeOf(condition_val);
        if (cond_type != LLVMInt1TypeInContext(ctx->context))
        {
            LLVMValueRef zero = LLVMConstNull(cond_type);
            condition_val = LLVMBuildICmp(ctx->builder, LLVMIntNE, condition_val, zero, "cond_bool");
        }

        LLVMBuildCondBr(ctx->builder, condition_val, body_block, after_block);

        // --- Emit body block ---
        LLVMPositionBuilderAtEnd(ctx->builder, body_block);
        process_ast_node(ctx, body_node);
        // If the body block doesn't already have a terminator, jump back to condition
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
        {
            LLVMBuildBr(ctx->builder, cond_block);
        }

        // --- Continue from after block ---
        LLVMPositionBuilderAtEnd(ctx->builder, after_block);
        break;
    }
    case AST_NODE_IF_STATEMENT:
    {
        // AST structure for IfStatement: [ConditionExpression, ThenStatement, (Optional) ElseStatement]
        if (node->data.list.count < 2)
        {
            fprintf(stderr, "IRGen Error: Invalid IfStatement AST node.\n");
            return;
        }

        c_grammar_node_t * condition_node = node->data.list.children[0];
        c_grammar_node_t * then_node = node->data.list.children[1];
        c_grammar_node_t * else_node = (node->data.list.count > 2) ? node->data.list.children[2] : NULL;

        LLVMValueRef condition_val = process_expression(ctx, condition_node);
        if (!condition_val)
        {
            fprintf(stderr, "IRGen Error: Failed to process condition for IfStatement.\n");
            return;
        }

        LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

        LLVMBasicBlockRef then_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "then");
        LLVMBasicBlockRef else_block
            = else_node ? LLVMAppendBasicBlockInContext(ctx->context, current_func, "else") : NULL;
        LLVMBasicBlockRef merge_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "if_merge");

        if (else_node)
        {
            LLVMBuildCondBr(ctx->builder, condition_val, then_block, else_block);
        }
        else
        {
            LLVMBuildCondBr(ctx->builder, condition_val, then_block, merge_block);
        }

        // --- Emit 'then' block ---
        LLVMPositionBuilderAtEnd(ctx->builder, then_block);
        process_ast_node(ctx, then_node);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
        {
            LLVMBuildBr(ctx->builder, merge_block);
        }

        // --- Emit 'else' block if present ---
        if (else_node)
        {
            LLVMPositionBuilderAtEnd(ctx->builder, else_block);
            process_ast_node(ctx, else_node);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
            {
                LLVMBuildBr(ctx->builder, merge_block);
            }
        }

        // --- Continue from merge block ---
        LLVMPositionBuilderAtEnd(ctx->builder, merge_block);
        break;
    }
    case AST_NODE_RETURN_STATEMENT:
    {
        // Handle 'return expression;' or 'return;'.
        if (node->data.list.count > 0 && node->data.list.children)
        {
            // Process the return expression.
            c_grammar_node_t * expr_node = node->data.list.children[0];
            LLVMValueRef return_value = process_expression(ctx, expr_node);
            if (return_value)
            {
                LLVMBuildRet(ctx->builder, return_value);
            }
            else
            {
                fprintf(stderr, "IRGen Error: Failed to process return expression.\n");
            }
        }
        else
        {
            // Handle 'return;' (e.g., void function or default return).
            // Assuming 'int' return type, so return 0.
            LLVMBuildRet(ctx->builder, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false));
        }
        break;
    }
    case AST_NODE_STRUCT_DEFINITION:
    {
        fprintf(stderr, "IRGen: Processing struct definition with %zu children\n", node->data.list.count);
        for (size_t i = 0; i < node->data.list.count; ++i)
        {
            c_grammar_node_t * child = node->data.list.children[i];
            if (child)
            {
                fprintf(stderr, "IRGen:   Child %zu: type=%d\n", i, child->type);
            }
            else
            {
                fprintf(stderr, "IRGen:   Child %zu: NULL\n", i);
            }
        }
        break;
    }
        // --- Add cases for other AST_NODE types ---
        // Examples: AST_NODE_BINARY_OP, AST_NODE_UNARY_OP, AST_NODE_FUNCTION_CALL,
        // AST_NODE_IF_STATEMENT, AST_NODE_WHILE_STATEMENT, AST_NODE_FOR_STATEMENT, etc.
        // Each requires specific LLVM IR generation logic.

    default:
        // Fallback: Recursively process children for unhandled node types.
        if (node->is_terminal_node)
        {
            // Do nothing for terminal nodes unless handled above
        }
        else if (node->data.list.children != NULL)
        {
            for (size_t i = 0; i < node->data.list.count; ++i)
            {
                process_ast_node(ctx, node->data.list.children[i]);
            }
        }
        break;
    }
}

// --- LLVM IR Helper Functions ---

/**
 * @brief Writes the LLVM IR module to a file in human-readable format.
 * @param module The LLVM module to write.
 * @param file_path The path to the output file.
 * @return 0 on success, -1 on failure.
 */
int
write_llvm_ir_to_file(LLVMModuleRef module, char const * file_path)
{
    if (!module || !file_path)
    {
        fprintf(stderr, "IRGen Error: Invalid module or file path for writing IR.\n");
        return -1;
    }

    char * error_message = NULL;
    // LLVMPrintModuleToFile writes human-readable IR.
    if (LLVMPrintModuleToFile(module, file_path, &error_message))
    {
        fprintf(stderr, "IRGen Error: Failed to write LLVM IR to file '%s': %s\n", file_path, error_message);
        LLVMDisposeMessage(error_message); // Dispose the error message string
        return -1;
    }

    // If successful, error_message will be NULL.
    printf("IRGen: Successfully wrote LLVM IR to %s\n", file_path);
    return 0;
}

/**
 * @brief Compiles the LLVM module to an object file or assembly file.
 * @param module The LLVM module to compile.
 * @param file_path The path to the output file.
 * @param march The target architecture (e.g., "x86-64").
 * @param file_type The type of file to emit (LLVMObjectFile or LLVMAssemblyFile).
 * @return 0 on success, -1 on failure.
 */
int
emit_to_file(LLVMModuleRef module, char const * file_path, char const * march, LLVMCodeGenFileType file_type)
{
    if (!module || !file_path)
    {
        fprintf(stderr, "IRGen Error: Invalid module or file path for emission.\n");
        return -1;
    }

    // Initialize X86 target
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmParser();
    LLVMInitializeX86AsmPrinter();

    char * error = NULL;
    char * triple = LLVMGetDefaultTargetTriple();

    // If march is specified, we might want to adjust the triple or find a specific target.
    // For now, we'll support "x86-64" by ensuring the triple matches.
    if (march && strcmp(march, "x86-64") == 0)
    {
        // On many systems, the default triple will already be x86_64-...
        // If we want to be explicit, we can override it.
        // LLVMDisposeMessage(triple);
        // triple = strdup("x86_64-pc-linux-gnu"); // Example explicit triple
    }

    LLVMTargetRef target;
    if (LLVMGetTargetFromTriple(triple, &target, &error))
    {
        fprintf(stderr, "IRGen Error: Failed to get target from triple '%s': %s\n", triple, error);
        LLVMDisposeMessage(error);
        LLVMDisposeMessage(triple);
        return -1;
    }

    // Create target machine
    // Use generic CPU and no features for now.
    LLVMTargetMachineRef target_machine = LLVMCreateTargetMachine(
        target, triple, "generic", "", LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault
    );

    if (!target_machine)
    {
        fprintf(stderr, "IRGen Error: Failed to create target machine.\n");
        LLVMDisposeMessage(triple);
        return -1;
    }

    // Set module's data layout and triple
    LLVMTargetDataRef data_layout = LLVMCreateTargetDataLayout(target_machine);
    char * data_layout_str = LLVMCopyStringRepOfTargetData(data_layout);
    LLVMSetDataLayout(module, data_layout_str);
    LLVMSetTarget(module, triple);

    // Emit to file
    if (LLVMTargetMachineEmitToFile(target_machine, module, (char *)file_path, file_type, &error))
    {
        fprintf(stderr, "IRGen Error: Failed to emit file '%s': %s\n", file_path, error);
        LLVMDisposeMessage(error);
        LLVMDisposeMessage(data_layout_str);
        LLVMDisposeTargetData(data_layout);
        LLVMDisposeTargetMachine(target_machine);
        LLVMDisposeMessage(triple);
        return -1;
    }

    printf(
        "IRGen: Successfully emitted %s to %s\n", (file_type == LLVMObjectFile) ? "object code" : "assembly", file_path
    );

    // Cleanup
    LLVMDisposeMessage(data_layout_str);
    LLVMDisposeTargetData(data_layout);
    LLVMDisposeTargetMachine(target_machine);
    LLVMDisposeMessage(triple);

    return 0;
}

// --- Placeholder for Symbol Table Management ---
// This is a crucial part of IR generation and needs to be robust.
// For this example, it's conceptual. A real implementation would use a hash map.

// Example of a conceptual symbol table entry
// typedef struct symbol_table_entry {
//     char* name;
//     LLVMValueRef ptr; // Pointer to the allocated variable (or function reference)
//     LLVMTypeRef type;
//     // Potentially scope information
// } symbol_table_entry_t;

// Placeholder function to add a symbol to the symbol table.
// static void add_symbol(ir_generator_ctx_t* ctx, const char* name, LLVMValueRef ptr, LLVMTypeRef type) {
//     // TODO: Implement symbol table addition logic.
//     fprintf(stderr, "IRGen: Conceptual add_symbol for '%s'\n", name);
// }

/**
 * @brief Gets the LLVM ValueRef representing the pointer to a variable.
 * Looks up the symbol in the symbol table.
 * @param ctx The IR generator context.
 * @param identifier_node The AST node for the identifier.
 * @param out_type Pointer to store the found LLVMTypeRef (element type).
 * @return The LLVM ValueRef (pointer) if found, NULL otherwise.
 */
static LLVMValueRef
get_variable_pointer(
    ir_generator_ctx_t * ctx,
    c_grammar_node_t * identifier_node,
    LLVMTypeRef * out_type
) // Added out_type parameter
{
    if (!identifier_node || identifier_node->type != AST_NODE_IDENTIFIER || !identifier_node->data.terminal.text)
    {
        fprintf(stderr, "IRGen Error: Invalid identifier node for get_variable_pointer.\n");
        return NULL;
    }
    char const * name = identifier_node->data.terminal.text;

    LLVMValueRef var_ptr;
    LLVMTypeRef retrieved_type; // Use a different name to avoid confusion

    if (find_symbol(ctx, name, &var_ptr, &retrieved_type))
    {
        if (out_type)
            *out_type = retrieved_type; // Pass the type back
        return var_ptr;
    }
    else
    {
        return NULL;
    }
}

/**
 * @brief Processes an expression AST node and returns its LLVM ValueRef.
 * This function recursively handles various expression types.
 */
static LLVMValueRef
process_expression(ir_generator_ctx_t * ctx, c_grammar_node_t * node)
{
    if (!node)
        return NULL;

    switch (node->type)
    {
    case AST_NODE_INTEGER_VALUE:
    {
        /* Expecting two child nodes - base value and suffix. */
        c_grammar_node_t * base_node = node->data.list.children[0];
        c_grammar_node_t * suffix_node = (node->data.list.count > 1) ? node->data.list.children[1] : NULL;

        char * base_text = base_node->data.terminal.text;
        char * suffix_text = (suffix_node && suffix_node->is_terminal_node) ? suffix_node->data.terminal.text : "";

        // Parse with base 0 to automatically handle 0x (hex) and 0 (octal)
        unsigned long long value = strtoull(base_text, NULL, 0);

        LLVMTypeRef int_type = LLVMInt32TypeInContext(ctx->context);
        bool is_unsigned = false;

        if (suffix_text && strlen(suffix_text) > 0)
        {
            if (strchr(suffix_text, 'u') || strchr(suffix_text, 'U'))
            {
                is_unsigned = true;
            }
            if (strstr(suffix_text, "ll") || strstr(suffix_text, "LL") || strchr(suffix_text, 'l')
                || strchr(suffix_text, 'L'))
            {
                int_type = LLVMInt64TypeInContext(ctx->context);
            }
        }

        return LLVMConstInt(int_type, value, !is_unsigned);
    }
    case AST_NODE_FLOAT_VALUE:
    {
        /* Expecting two child nodes - base value and suffix. */
        c_grammar_node_t * base_node = node->data.list.children[0];
        c_grammar_node_t * suffix_node = (node->data.list.count > 1) ? node->data.list.children[1] : NULL;

        char * base_text = base_node->data.terminal.text;
        char * suffix_text = (suffix_node && suffix_node->is_terminal_node) ? suffix_node->data.terminal.text : "";

        char * full_text = NULL;
        asprintf(&full_text, "%s%s", base_text, suffix_text);

        long double value = strtold(full_text, NULL);

        LLVMTypeRef float_type = LLVMDoubleTypeInContext(ctx->context); // Default to double
        if (suffix_text && strlen(suffix_text) > 0)
        {
            if (strchr(suffix_text, 'f') || strchr(suffix_text, 'F'))
            {
                float_type = LLVMFloatTypeInContext(ctx->context);
            }
            else if (strchr(suffix_text, 'l') || strchr(suffix_text, 'L'))
            {
                float_type = LLVMX86FP80TypeInContext(ctx->context);
            }
        }

        free(full_text);
        return LLVMConstReal(float_type, value);
    }
    case AST_NODE_STRING_LITERAL:
    {
        // Handle string literals like "Hello".
        if (node->data.terminal.text)
        {
            char * raw_text = node->data.terminal.text;
            size_t len = strlen(raw_text);
            if (len >= 2 && raw_text[0] == '"' && raw_text[len - 1] == '"')
            {
                char * stripped = strndup(raw_text + 1, len - 2);
                char * decoded = decode_string(stripped);
                LLVMValueRef global_str = LLVMBuildGlobalStringPtr(ctx->builder, decoded, "str_tmp");
                free(decoded);
                free(stripped);
                return global_str;
            }
            char * decoded = decode_string(raw_text);
            LLVMValueRef global_str = LLVMBuildGlobalStringPtr(ctx->builder, decoded, "str_tmp");
            free(decoded);
            return global_str;
        }
        break;
    }
    case AST_NODE_POSTFIX_EXPRESSION:
    {
        // AST structure for PostfixExpression: [BaseExpression, SuffixPart1, SuffixPart2, ...]
        if (node->data.list.count < 2)
        {
            return process_expression(ctx, node->data.list.children[0]);
        }

        c_grammar_node_t * base_node = node->data.list.children[0];
        LLVMValueRef base_val = NULL;

        // Special handling if base is an identifier being called as a function
        if (base_node->type == AST_NODE_IDENTIFIER)
        {
            char const * func_name = base_node->data.terminal.text;
            base_val = LLVMGetNamedFunction(ctx->module, func_name);
        }

        if (!base_val)
        {
            base_val = process_expression(ctx, base_node);
        }

        for (size_t i = 1; i < node->data.list.count; ++i)
        {
            c_grammar_node_t * suffix = node->data.list.children[i];
            if (suffix->type == AST_NODE_FUNCTION_CALL)
            {
                // Handle function call. Arguments might be children directly or in an ArgumentList
                size_t num_args = 0;
                LLVMValueRef * args = NULL;

                if (suffix->data.list.count > 0)
                {
                    num_args = suffix->data.list.count;
                    args = malloc(num_args * sizeof(LLVMValueRef));
                    for (size_t j = 0; j < num_args; ++j)
                    {
                        args[j] = process_expression(ctx, suffix->data.list.children[j]);
                    }
                }

                if (!base_val)
                {
                    if (base_node->type == AST_NODE_IDENTIFIER)
                    {
                        char const * func_name = base_node->data.terminal.text;
                        // For undeclared functions like printf, auto-declare as variadic returning i32
                        LLVMTypeRef ret_type = LLVMInt32TypeInContext(ctx->context);
                        LLVMTypeRef * arg_types = malloc(num_args * sizeof(LLVMTypeRef));
                        for (size_t j = 0; j < num_args; ++j)
                        {
                            arg_types[j] = LLVMTypeOf(args[j]);
                        }
                        LLVMTypeRef func_type = LLVMFunctionType(ret_type, arg_types, (unsigned)num_args, true);
                        base_val = LLVMAddFunction(ctx->module, func_name, func_type);
                        free(arg_types);
                    }
                    else
                    {
                        fprintf(stderr, "IRGen Error: Could not resolve function for call.\n");
                        if (args) free(args);
                        return NULL;
                    }
                }

                LLVMTypeRef func_type = LLVMGlobalGetValueType(base_val);
                base_val = LLVMBuildCall2(ctx->builder, func_type, base_val, args, (unsigned)num_args, "call_tmp");
                if (args)
                    free(args);
            }
            else if (suffix->type == AST_NODE_ARRAY_SUBSCRIPT)
            {
                // Array subscript: [LBracket, IndexExpression, RBracket]
                // Find the index expression
                LLVMValueRef index_val = NULL;
                for (size_t k = 0; k < suffix->data.list.count; ++k)
                {
                    c_grammar_node_t * child = suffix->data.list.children[k];
                    if (child && !child->is_terminal_node)
                    {
                        index_val = process_expression(ctx, child);
                        break;
                    }
                }

                if (index_val && base_node && base_node->type == AST_NODE_IDENTIFIER)
                {
                    char const * arr_name = base_node->data.terminal.text;
                    LLVMValueRef arr_ptr;
                    LLVMTypeRef arr_type;
                    if (find_symbol(ctx, arr_name, &arr_ptr, &arr_type))
                    {
                        // Create GEP to access element
                        LLVMValueRef indices[2];
                        indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                        indices[1] = index_val;
                        LLVMValueRef elem_ptr = LLVMBuildInBoundsGEP2(ctx->builder, arr_type, arr_ptr, indices, 2, "arrayidx");

                        // Get element type (array type's element type)
                        LLVMTypeRef elem_type;
                        if (LLVMGetTypeKind(arr_type) == LLVMPointerTypeKind)
                        {
                            elem_type = LLVMPointerType(LLVMGetElementType(arr_type), 0);
                        }
                        else
                        {
                            elem_type = LLVMGetElementType(arr_type);
                        }
                        base_val = LLVMBuildLoad2(ctx->builder, elem_type, elem_ptr, "arrayelem");
                    }
                }
            }
            else if (suffix->type == AST_NODE_MEMBER_ACCESS_DOT || suffix->type == AST_NODE_MEMBER_ACCESS_ARROW)
            {
                // Struct member access: s.x or p->x
                // AST_MEMBER_ACCESS_DOT/ARROW children: [Dot/Arrow, Identifier]
                // Find the member name
                char * member_name = NULL;
                for (size_t k = 0; k < suffix->data.list.count; ++k)
                {
                    c_grammar_node_t * child = suffix->data.list.children[k];
                    if (child && child->type == AST_NODE_IDENTIFIER)
                    {
                        member_name = child->data.terminal.text;
                        break;
                    }
                }

                if (member_name && base_val)
                {
                    // Get the struct type
                    LLVMTypeRef base_type = LLVMTypeOf(base_val);
                    LLVMTypeRef struct_type;
                    bool is_arrow = (suffix->type == AST_NODE_MEMBER_ACCESS_ARROW);

                    if (is_arrow)
                    {
                        // For arrow, base_val is already a pointer to struct
                        struct_type = LLVMGetElementType(base_type);
                    }
                    else
                    {
                        // For dot, base_val might be a struct value or pointer
                        if (LLVMGetTypeKind(base_type) == LLVMPointerTypeKind)
                        {
                            struct_type = LLVMGetElementType(base_type);
                        }
                        else
                        {
                            struct_type = base_type;
                        }
                    }

                    // Find the member index by name
                    unsigned num_elements = LLVMCountStructElementTypes(struct_type);
                    unsigned member_index = 0;
                    bool found = false;
                    
                    // Look up the struct info to find the member by name
                    struct_info_t * struct_info = NULL;
                    for (size_t si = 0; si < ctx->struct_count; si++)
                    {
                        if (ctx->structs[si].type == struct_type)
                        {
                            struct_info = &ctx->structs[si];
                            break;
                        }
                    }
                    
                    if (struct_info && struct_info->fields)
                    {
                        for (unsigned j = 0; j < num_elements && j < struct_info->field_count; ++j)
                        {
                            if (struct_info->fields[j].name && strcmp(struct_info->fields[j].name, member_name) == 0)
                            {
                                member_index = j;
                                found = true;
                                break;
                            }
                        }
                    }
                    else
                    {
                        // Fallback: assume members are in order and match the field_count
                        for (unsigned j = 0; j < num_elements; ++j)
                        {
                            if (j == 0)
                            {
                                member_index = j;
                                found = true;
                                break;
                            }
                        }
                    }

                    if (found || num_elements > 0)
                    {
                        // Create GEP to access member
                        LLVMValueRef indices[2];
                        indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                        indices[1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), member_index, false);

                        LLVMValueRef member_ptr;
                        if (is_arrow || LLVMGetTypeKind(base_type) == LLVMPointerTypeKind)
                        {
                            member_ptr = LLVMBuildInBoundsGEP2(ctx->builder, struct_type, base_val, indices, 2, "memberptr");
                        }
                        else
                        {
                            // For value types, we need to get the pointer
                            LLVMValueRef struct_ptr = LLVMBuildAlloca(ctx->builder, struct_type, "struct_tmp");
                            LLVMBuildStore(ctx->builder, base_val, struct_ptr);
                            member_ptr = LLVMBuildInBoundsGEP2(ctx->builder, struct_type, struct_ptr, indices, 2, "memberptr");
                        }

                        LLVMTypeRef member_type = LLVMStructGetTypeAtIndex(struct_type, member_index);
                        base_val = LLVMBuildLoad2(ctx->builder, member_type, member_ptr, "member");
                    }
                }
            }
            else
            {
                fprintf(stderr, "IRGen Warning: Unhandled postfix suffix type %u\n", suffix->type);
            }
        }

        return base_val;
    }
    case AST_NODE_FUNCTION_CALL:
    {
        // This node type is typically handled as a suffix in AST_NODE_POSTFIX_EXPRESSION.
        // If it's encountered on its own, it likely means arguments.
        // Recursive traversal should yield the values.
        if (node->data.list.count > 0)
        {
            // For now, just return the first argument if processed in isolation (rare)
            return process_expression(ctx, node->data.list.children[0]);
        }
        return NULL;
    }
    case AST_NODE_CAST_EXPRESSION:
    {
        // AST structure for CastExpression: [TypeName, CastExpression]
        if (node->data.list.count < 2)
            return NULL;

        c_grammar_node_t * type_name_node = node->data.list.children[0];
        c_grammar_node_t * inner_expr_node = node->data.list.children[1];

        if (type_name_node->type != AST_NODE_TYPE_NAME)
        {
            fprintf(stderr, "IRGen Error: Expected TypeName in cast expression, got %u\n", type_name_node->type);
            return NULL;
        }

        // TypeName children are [SpecifierQualifierList, AbstractDeclarator?]
        c_grammar_node_t * spec_qual = type_name_node->data.list.children[0];
        c_grammar_node_t * abstract_decl = (type_name_node->data.list.count > 1) ? type_name_node->data.list.children[1] : NULL;

        LLVMTypeRef target_type = map_type(ctx, spec_qual, abstract_decl);
        LLVMValueRef val_to_cast = process_expression(ctx, inner_expr_node);

        if (val_to_cast && target_type)
        {
            LLVMTypeRef src_type = LLVMTypeOf(val_to_cast);
            if (LLVMGetTypeKind(target_type) == LLVMIntegerTypeKind && (LLVMGetTypeKind(src_type) == LLVMFloatTypeKind || LLVMGetTypeKind(src_type) == LLVMDoubleTypeKind))
            {
                return LLVMBuildFPToSI(ctx->builder, val_to_cast, target_type, "casttmp");
            }
            else if ((LLVMGetTypeKind(target_type) == LLVMFloatTypeKind || LLVMGetTypeKind(target_type) == LLVMDoubleTypeKind) && LLVMGetTypeKind(src_type) == LLVMIntegerTypeKind)
            {
                return LLVMBuildSIToFP(ctx->builder, val_to_cast, target_type, "casttmp");
            }
            // Add more cast types as needed (bitcast, pointer casts, etc.)
            return val_to_cast; // Fallback
        }
        return NULL;
    }
    case AST_NODE_ASSIGNMENT:
    {
        if (node->data.list.count >= 3 && node->data.list.children)
        {
            c_grammar_node_t * lhs_node = node->data.list.children[0];
            c_grammar_node_t * rhs_node = node->data.list.children[2];

            LLVMValueRef lhs_ptr = NULL;
            LLVMTypeRef lhs_type = NULL;

            // Check if LHS is a PostfixExpression with array subscript or member access
            if (lhs_node->type == AST_NODE_POSTFIX_EXPRESSION && lhs_node->data.list.count >= 2)
            {
                c_grammar_node_t * base_node = lhs_node->data.list.children[0];
                if (base_node->type == AST_NODE_IDENTIFIER)
                {
                    char const * base_name = base_node->data.terminal.text;
                    LLVMValueRef base_ptr;
                    LLVMTypeRef base_type;
                    if (find_symbol(ctx, base_name, &base_ptr, &base_type))
                    {
                        LLVMValueRef current_ptr = base_ptr;
                        LLVMTypeRef current_type = base_type;
                        
                        // Process suffixes to handle array subscripts and member access
                        for (size_t i = 1; i < lhs_node->data.list.count; ++i)
                        {
                            c_grammar_node_t * suffix = lhs_node->data.list.children[i];
                            
                            if (suffix->type == AST_NODE_ARRAY_SUBSCRIPT)
                            {
                                LLVMValueRef index_val = NULL;
                                for (size_t k = 0; k < suffix->data.list.count; ++k)
                                {
                                    c_grammar_node_t * child = suffix->data.list.children[k];
                                    if (child && !child->is_terminal_node)
                                    {
                                        index_val = process_expression(ctx, child);
                                        break;
                                    }
                                }
                                
                                if (index_val && current_type)
                                {
                                    LLVMTypeRef elem_type = NULL;
                                    if (LLVMGetTypeKind(current_type) == LLVMPointerTypeKind)
                                    {
                                        elem_type = LLVMGetElementType(current_type);
                                    }
                                    else if (LLVMGetTypeKind(current_type) == LLVMArrayTypeKind)
                                    {
                                        elem_type = LLVMGetElementType(current_type);
                                    }
                                    
                                    LLVMValueRef indices[2];
                                    indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                                    indices[1] = index_val;
                                    current_ptr = LLVMBuildInBoundsGEP2(ctx->builder, elem_type, current_ptr, indices, 2, "arrayidx");
                                    current_type = elem_type;
                                }
                            }
                            else if (suffix->type == AST_NODE_MEMBER_ACCESS_DOT || suffix->type == AST_NODE_MEMBER_ACCESS_ARROW)
                            {
                                char * member_name = NULL;
                                for (size_t k = 0; k < suffix->data.list.count; ++k)
                                {
                                    c_grammar_node_t * child = suffix->data.list.children[k];
                                    if (child && child->type == AST_NODE_IDENTIFIER)
                                    {
                                        member_name = child->data.terminal.text;
                                        break;
                                    }
                                }
                                
                                if (member_name && current_ptr && current_type)
                                {
                                    LLVMTypeRef struct_type = current_type;
                                    if (LLVMGetTypeKind(struct_type) == LLVMPointerTypeKind)
                                    {
                                        struct_type = LLVMGetElementType(struct_type);
                                    }
                                    
                                    if (LLVMGetTypeKind(struct_type) == LLVMStructTypeKind)
                                    {
                                        unsigned num_elements = LLVMCountStructElementTypes(struct_type);
                                        unsigned member_index = 0;
                                        struct_info_t *info = NULL;
                                        
                                        for (size_t si = 0; si < ctx->struct_count; si++)
                                        {
                                            if (ctx->structs[si].type == struct_type)
                                            {
                                                info = &ctx->structs[si];
                                                break;
                                            }
                                        }
                                        
                                        if (info)
                                        {
                                            for (unsigned j = 0; j < num_elements && j < info->field_count; j++)
                                            {
                                                if (info->fields[j].name && strcmp(info->fields[j].name, member_name) == 0)
                                                {
                                                    member_index = j;
                                                    break;
                                                }
                                            }
                                        }
                                        
                                        LLVMValueRef indices[2];
                                        indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                                        indices[1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), member_index, false);
                                        current_ptr = LLVMBuildInBoundsGEP2(ctx->builder, struct_type, current_ptr, indices, 2, "memberptr");
                                        current_type = LLVMStructGetTypeAtIndex(struct_type, member_index);
                                    }
                                }
                            }
                        }
                        
                        lhs_ptr = current_ptr;
                        lhs_type = current_type;
                    }
                }
            }
            else
            {
                // Simple variable assignment
                lhs_ptr = get_variable_pointer(ctx, lhs_node, &lhs_type);
            }

            if (!lhs_ptr)
            {
                fprintf(stderr, "IRGen Error: Could not get pointer for LHS in assignment.\n");
                return NULL;
            }

            // Process the RHS expression to get its LLVM ValueRef.
            LLVMValueRef rhs_value = process_expression(ctx, rhs_node);
            if (!rhs_value)
            {
                fprintf(stderr, "IRGen Error: Failed to process RHS expression in assignment.\n");
                return NULL;
            }

            // Generate the store instruction.
            LLVMBuildStore(ctx->builder, rhs_value, lhs_ptr);
            return rhs_value;
        }
        return NULL;
    }
    case AST_NODE_IDENTIFIER:
    {
        LLVMValueRef var_ptr;
        LLVMTypeRef element_type; // This will hold the type (e.g., i32)

        // Get the variable's pointer and its element type from the symbol table.
        var_ptr = get_variable_pointer(ctx, node, &element_type);

        if (var_ptr && element_type) // Ensure both are valid
        {
            // Load the value from the memory address using LLVMBuildLoad2.
            return LLVMBuildLoad2(ctx->builder, element_type, var_ptr, "load_tmp"); // "load_tmp" is a debug name.
        }
        else
        {
            // Check if it's a function name before reporting error
            if (LLVMGetNamedFunction(ctx->module, node->data.terminal.text))
            {
                return NULL; // It's a function, not a variable to load from
            }
            fprintf(stderr, "IRGen Error: Undefined variable '%s' used.\n", node->data.terminal.text);
            return NULL;
        }
        break;
    }
    case AST_NODE_BINARY_OP:
    case AST_NODE_RELATIONAL_EXPRESSION:
    case AST_NODE_EQUALITY_EXPRESSION:
    case AST_NODE_AND_EXPRESSION:
    case AST_NODE_EXCLUSIVE_OR_EXPRESSION:
    case AST_NODE_INCLUSIVE_OR_EXPRESSION:
    {
        // Handle binary operations like '+', '-', '*', '/', etc.
        // chainl1 can produce nested structures.
        if (node->data.list.count == 1)
        {
            return process_expression(ctx, node->data.list.children[0]);
        }

        if (node->data.list.count >= 2 && node->data.list.children)
        {
            LLVMValueRef lhs_val = NULL;
            LLVMValueRef rhs_val = NULL;
            char const * op_str = NULL;

            if (node->data.list.count == 2)
            {
                // Bitwise ops from chainl1: [LHS, RHS], operator is implied by node type
                lhs_val = process_expression(ctx, node->data.list.children[0]);
                rhs_val = process_expression(ctx, node->data.list.children[1]);
                if (node->type == AST_NODE_AND_EXPRESSION) op_str = "&";
                else if (node->type == AST_NODE_EXCLUSIVE_OR_EXPRESSION) op_str = "^";
                else if (node->type == AST_NODE_INCLUSIVE_OR_EXPRESSION) op_str = "|";
            }
            else if (node->data.list.count >= 3)
            {
                // Standard binary ops: [LHS, OP, RHS]
                lhs_val = process_expression(ctx, node->data.list.children[0]);
                rhs_val = process_expression(ctx, node->data.list.children[2]);
                c_grammar_node_t * op_node = node->data.list.children[1];
                if (op_node->is_terminal_node) op_str = op_node->data.terminal.text;
            }

            if (lhs_val && rhs_val && op_str)
            {
                LLVMTypeRef lhs_type = LLVMTypeOf(lhs_val);
                LLVMTypeKind type_kind = LLVMGetTypeKind(lhs_type);

                // Check if the type is a float or double
                bool is_float_op = (type_kind == LLVMFloatTypeKind || type_kind == LLVMDoubleTypeKind);

                if (strcmp(op_str, "+") == 0)
                    return is_float_op ? LLVMBuildFAdd(ctx->builder, lhs_val, rhs_val, "fadd_tmp")
                                       : LLVMBuildAdd(ctx->builder, lhs_val, rhs_val, "add_tmp");
                if (strcmp(op_str, "-") == 0)
                    return is_float_op ? LLVMBuildFSub(ctx->builder, lhs_val, rhs_val, "fsub_tmp")
                                       : LLVMBuildSub(ctx->builder, lhs_val, rhs_val, "sub_tmp");
                if (strcmp(op_str, "*") == 0)
                    return is_float_op ? LLVMBuildFMul(ctx->builder, lhs_val, rhs_val, "fmul_tmp")
                                       : LLVMBuildMul(ctx->builder, lhs_val, rhs_val, "mul_tmp");
                if (strcmp(op_str, "/") == 0)
                    return is_float_op ? LLVMBuildFDiv(ctx->builder, lhs_val, rhs_val, "fdiv_tmp")
                                       : LLVMBuildSDiv(ctx->builder, lhs_val, rhs_val, "div_tmp");
                if (strcmp(op_str, "%") == 0)
                    return is_float_op ? LLVMBuildFRem(ctx->builder, lhs_val, rhs_val, "frem_tmp")
                                       : LLVMBuildSRem(ctx->builder, lhs_val, rhs_val, "rem_tmp");

                if (strcmp(op_str, "==") == 0)
                    return is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, lhs_val, rhs_val, "feq_tmp")
                                       : LLVMBuildICmp(ctx->builder, LLVMIntEQ, lhs_val, rhs_val, "eq_tmp");
                if (strcmp(op_str, "!=") == 0)
                    return is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealONE, lhs_val, rhs_val, "fne_tmp")
                                       : LLVMBuildICmp(ctx->builder, LLVMIntNE, lhs_val, rhs_val, "ne_tmp");
                if (strcmp(op_str, "<") == 0)
                    return is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOLT, lhs_val, rhs_val, "flt_tmp")
                                       : LLVMBuildICmp(ctx->builder, LLVMIntSLT, lhs_val, rhs_val, "lt_tmp");
                if (strcmp(op_str, ">") == 0)
                    return is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOGT, lhs_val, rhs_val, "fgt_tmp")
                                       : LLVMBuildICmp(ctx->builder, LLVMIntSGT, lhs_val, rhs_val, "gt_tmp");
                if (strcmp(op_str, "<=") == 0)
                    return is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOLE, lhs_val, rhs_val, "fle_tmp")
                                       : LLVMBuildICmp(ctx->builder, LLVMIntSLE, lhs_val, rhs_val, "le_tmp");
                if (strcmp(op_str, ">=") == 0)
                    return is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOGE, lhs_val, rhs_val, "fge_tmp")
                                       : LLVMBuildICmp(ctx->builder, LLVMIntSGE, lhs_val, rhs_val, "ge_tmp");

                // Bitwise Operators
                if (strcmp(op_str, "&") == 0) return LLVMBuildAnd(ctx->builder, lhs_val, rhs_val, "and_tmp");
                if (strcmp(op_str, "|") == 0) return LLVMBuildOr(ctx->builder, lhs_val, rhs_val, "or_tmp");
                if (strcmp(op_str, "^") == 0) return LLVMBuildXor(ctx->builder, lhs_val, rhs_val, "xor_tmp");
                if (strcmp(op_str, "<<") == 0) return LLVMBuildShl(ctx->builder, lhs_val, rhs_val, "shl_tmp");
                if (strcmp(op_str, ">>") == 0) return LLVMBuildAShr(ctx->builder, lhs_val, rhs_val, "ashr_tmp");
            }
        }
        return NULL;
    }
    case AST_NODE_LOGICAL_AND_EXPRESSION:
    case AST_NODE_LOGICAL_OR_EXPRESSION:
    {
        if (node->data.list.count == 1)
        {
            return process_expression(ctx, node->data.list.children[0]);
        }

        bool is_or = (node->type == AST_NODE_LOGICAL_OR_EXPRESSION);
        c_grammar_node_t * lhs_node = node->data.list.children[0];
        c_grammar_node_t * rhs_node = node->data.list.children[2];

        LLVMValueRef res_alloca = LLVMBuildAlloca(ctx->builder, LLVMInt1TypeInContext(ctx->context), "logical_res");
        
        LLVMBasicBlockRef rhs_block = LLVMAppendBasicBlockInContext(ctx->context, LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder)), "logical_rhs");
        LLVMBasicBlockRef merge_block = LLVMAppendBasicBlockInContext(ctx->context, LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder)), "logical_merge");

        LLVMValueRef lhs_val = process_expression(ctx, lhs_node);
        // Convert to i1
        if (LLVMGetTypeKind(LLVMTypeOf(lhs_val)) != LLVMIntegerTypeKind || LLVMGetIntTypeWidth(LLVMTypeOf(lhs_val)) != 1)
        {
            lhs_val = LLVMBuildICmp(ctx->builder, LLVMIntNE, lhs_val, LLVMConstNull(LLVMTypeOf(lhs_val)), "bool_tmp");
        }

        LLVMBuildStore(ctx->builder, lhs_val, res_alloca);
        if (is_or)
        {
            LLVMBuildCondBr(ctx->builder, lhs_val, merge_block, rhs_block);
        }
        else
        {
            LLVMBuildCondBr(ctx->builder, lhs_val, rhs_block, merge_block);
        }

        LLVMPositionBuilderAtEnd(ctx->builder, rhs_block);
        LLVMValueRef rhs_val = process_expression(ctx, rhs_node);
        if (LLVMGetTypeKind(LLVMTypeOf(rhs_val)) != LLVMIntegerTypeKind || LLVMGetIntTypeWidth(LLVMTypeOf(rhs_val)) != 1)
        {
            rhs_val = LLVMBuildICmp(ctx->builder, LLVMIntNE, rhs_val, LLVMConstNull(LLVMTypeOf(rhs_val)), "bool_tmp");
        }
        LLVMBuildStore(ctx->builder, rhs_val, res_alloca);
        LLVMBuildBr(ctx->builder, merge_block);

        LLVMPositionBuilderAtEnd(ctx->builder, merge_block);
        return LLVMBuildLoad2(ctx->builder, LLVMInt1TypeInContext(ctx->context), res_alloca, "logical_final");
    }
    case AST_NODE_UNARY_OP:
    {
        // Unary structure: [Operator, Operand]
        if (node->data.list.count < 2) return NULL;
        c_grammar_node_t * op_node = node->data.list.children[0];
        c_grammar_node_t * operand_node = node->data.list.children[1];
        LLVMValueRef operand_val = process_expression(ctx, operand_node);
        if (!operand_val) return NULL;

        char const * op_str = op_node->data.terminal.text;
        if (strcmp(op_str, "-") == 0)
        {
            if (LLVMGetTypeKind(LLVMTypeOf(operand_val)) == LLVMFloatTypeKind || LLVMGetTypeKind(LLVMTypeOf(operand_val)) == LLVMDoubleTypeKind)
                return LLVMBuildFNeg(ctx->builder, operand_val, "fneg_tmp");
            return LLVMBuildNeg(ctx->builder, operand_val, "neg_tmp");
        }
        if (strcmp(op_str, "!") == 0)
        {
            LLVMValueRef is_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, operand_val, LLVMConstNull(LLVMTypeOf(operand_val)), "not_tmp");
            return is_zero;
        }
        if (strcmp(op_str, "~") == 0)
        {
            return LLVMBuildNot(ctx->builder, operand_val, "bitnot_tmp");
        }
        return operand_val;
    }
    // TODO: Add cases for other expression types (unary ops, function calls, etc.).
    default:
        // Attempt to recursively process if it might yield a value.
        if (!node->is_terminal_node && node->data.list.children != NULL)
        {
            for (size_t i = 0; i < node->data.list.count; ++i)
            {
                LLVMValueRef res = process_expression(ctx, node->data.list.children[i]);
                if (res)
                    return res; // Return the first valid result found.
            }
        }
        break;
    }
    return NULL; // Return NULL if expression processing failed or not implemented.
}
