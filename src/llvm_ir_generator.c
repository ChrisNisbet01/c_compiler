#include "llvm_ir_generator.h"
#include "c_grammar_ast.h" // Assumes this header defines c_grammar_node_t and its node types
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declare the context structure as it's used before its definition
// typedef struct ir_generator_ctx ir_generator_ctx_t; // Assuming this is declared in a header or elsewhere

// Symbol table management functions
static void add_symbol(ir_generator_ctx_t *ctx, const char *name, LLVMValueRef ptr, LLVMTypeRef type);
static LLVMValueRef get_variable_pointer(ir_generator_ctx_t *ctx, c_grammar_node_t *identifier_node);
static void free_symbol_table(ir_generator_ctx_t *ctx);
static bool find_symbol(ir_generator_ctx_t *ctx, const char *name, LLVMValueRef *out_ptr, LLVMTypeRef *out_type); // Helper for get_variable_pointer

// --- Forward Declarations ---
// Recursive function to process AST nodes
static void process_ast_node(ir_generator_ctx_t *ctx, c_grammar_node_t const *node);
// Helper function to process expressions and return LLVM ValueRef
static LLVMValueRef process_expression(ir_generator_ctx_t *ctx, c_grammar_node_t *node);
// Helper to get a variable's pointer from the symbol table (conceptual)
static LLVMValueRef get_variable_pointer(ir_generator_ctx_t *ctx, c_grammar_node_t *identifier_node);
// Placeholder for symbol table operations
// static void add_symbol(ir_generator_ctx_t* ctx, const char* name, LLVMValueRef ptr, LLVMTypeRef type);

/**
 * @brief Adds a symbol to the current scope's symbol table.
 * @param ctx The IR generator context.
 * @param name The name of the symbol.
 * @param ptr The LLVMValueRef pointer to the symbol's memory.
 * @param type The LLVMTypeRef of the symbol.
 */
static void add_symbol(ir_generator_ctx_t *ctx, const char *name, LLVMValueRef ptr, LLVMTypeRef type)
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
        symbol_t *new_table = realloc(ctx->symbol_table, new_capacity * sizeof(symbol_t));
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
static bool find_symbol(ir_generator_ctx_t *ctx, const char *name, LLVMValueRef *out_ptr, LLVMTypeRef *out_type)
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

// --- IR Generator Context Initialization and Disposal ---

/**
 * @brief Initializes the IR generator context.
 * Creates LLVM context, module, and builder.
 */
ir_generator_ctx_t *ir_generator_init()
{
    ir_generator_ctx_t *ctx = calloc(1, sizeof(ir_generator_ctx_t));
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

    return ctx;
}

/**
 * @brief Frees the symbol table memory.
 */
static void free_symbol_table(ir_generator_ctx_t *ctx)
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
void ir_generator_dispose(ir_generator_ctx_t *ctx)
{
    if (!ctx)
        return;

    free_symbol_table(ctx); // Free symbol table first

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
LLVMModuleRef generate_llvm_ir(ir_generator_ctx_t *ctx, c_grammar_node_t const *ast_root)
{
    if (!ctx || !ast_root)
    {
        fprintf(stderr, "IRGen: Invalid context or AST root provided.\n");
        return NULL;
    }

    // Start processing the AST from the root node.
    process_ast_node(ctx, ast_root);

    // The module is owned by the context.
    return ctx->module;
}

// --- AST Node Processing Logic ---

/**
 * @brief Recursively processes AST nodes to generate LLVM IR.
 * This function dispatches to specific handlers based on the node type.
 */
static void process_ast_node(ir_generator_ctx_t *ctx, c_grammar_node_t const *node)
{
    if (!node)
        return;

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
        // Simplified for 'main' function and 'int' return type.
        // A full implementation needs to parse return type, name, and parameters.

        c_grammar_node_t *decl_specifiers_node = NULL;
        c_grammar_node_t *declarator_node = NULL;
        c_grammar_node_t *compound_stmt_node = NULL;

        // Iterate through children to find relevant parts: DeclSpecifiers, Declarator, CompoundStatement.
        if (node->data.list.children)
        {
            for (size_t i = 0; i < node->data.list.count; ++i)
            {
                c_grammar_node_t *child = node->data.list.children[i];
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

        // --- Extract Function Name and Return Type ---
        // Simplified extraction for 'main' and 'int'.
        char *func_name = "unknown_function";                           // Default name.
        LLVMTypeRef return_type = LLVMInt32TypeInContext(ctx->context); // Default return type is int.

        // Parse declarator to get function name.
        // Assumes a direct identifier in the DirectDeclarator.
        if (declarator_node->data.list.count > 0 && declarator_node->data.list.children[0]->type == AST_NODE_DIRECT_DECLARATOR)
        {
            c_grammar_node_t *direct_decl = declarator_node->data.list.children[0];
            if (direct_decl->data.list.count > 0 && direct_decl->data.list.children[0]->type == AST_NODE_IDENTIFIER)
            {
                func_name = direct_decl->data.list.children[0]->data.terminal.text;
            }
        }

        // TODO: Parse actual return type from decl_specifiers_node.

        // --- Create LLVM Function ---
        // Corrected: For a function with no parameters, pass NULL and 0.
        LLVMTypeRef param_types[] = {0};
        unsigned num_params = 0;
        LLVMTypeRef func_type = LLVMFunctionType(return_type, param_types, num_params, false);
        LLVMValueRef func = LLVMAddFunction(ctx->module, func_name, func_type);

        // Create a basic block for the function's entry point.
        LLVMBasicBlockRef entry_block = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
        // Position the builder at the end of the entry block.
        LLVMPositionBuilderAtEnd(ctx->builder, entry_block);

        // TODO: Handle function parameters: allocate space and store arguments.

        // Process the compound statement (function body).
        process_ast_node(ctx, compound_stmt_node);

        // --- Add a default return if the function doesn't end with one ---
        // This is a basic check for functions that don't explicitly return.
        LLVMValueRef last_instr = LLVMGetLastInstruction(entry_block);
        if (last_instr == NULL || LLVMIsAReturnInst(last_instr) == NULL)
        {
            // If no return instruction was generated, add one. For 'main', typically return 0.
            LLVMBuildRet(ctx->builder, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false));
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
    case AST_NODE_DECLARATION:
    {
        // --- Handle Variable Declarations ---
        // Example: 'int i = 42;'
        // Children are typically: [DeclSpecifiers, InitDeclaratorList] or [DeclSpecifiers, Declarator]

        LLVMTypeRef var_type = LLVMInt32TypeInContext(ctx->context); // Default type: int.
        // TODO: Parse actual type from DeclSpecifiers.

        // Process InitDeclarators to create variables and initialize them.
        for (size_t i = 0; i < node->data.list.count; ++i)
        {
            if (node->data.list.children[i]->type == AST_NODE_INIT_DECLARATOR)
            {
                c_grammar_node_t *init_decl_node = node->data.list.children[i];

                char *var_name = NULL;
                c_grammar_node_t *initializer_expr_node = NULL; // Node representing the initializer expression.

                // Simplified extraction of variable name from declarator.
                if (init_decl_node->data.list.count > 0 && init_decl_node->data.list.children[0]->type ==
                                                               AST_NODE_DECLARATOR)
                {
                    c_grammar_node_t *declarator_node = init_decl_node->data.list.children[0];
                    if (declarator_node->data.list.count > 0 &&
                        declarator_node->data.list.children[0]->type == AST_NODE_DIRECT_DECLARATOR)
                    {
                        c_grammar_node_t *direct_decl_node = declarator_node->data.list.children[0];
                        if (direct_decl_node->data.list.count > 0 &&
                            direct_decl_node->data.list.children[0]->type == AST_NODE_IDENTIFIER)
                        {
                            var_name = direct_decl_node->data.list.children[0]->data.terminal.text;
                        }
                    }
                }

                // Find initializer if it exists (e.g., '= 42').
                if (init_decl_node->data.list.count > 1 && init_decl_node->data.list.children[1]->type ==
                                                               AST_NODE_ASSIGNMENT)
                {
                    c_grammar_node_t *assignment_node = init_decl_node->data.list.children[1];
                    // The actual expression node is typically the third child of AST_NODE_ASSIGNMENT.
                    if (assignment_node->data.list.count > 2 && assignment_node->data.list.children[2])
                    {
                        initializer_expr_node = assignment_node->data.list.children[2];
                    }
                }

                if (var_name)
                {
                    // Allocate space for the variable on the stack.
                    LLVMValueRef alloca_inst = LLVMBuildAlloca(ctx->builder, var_type, var_name);

                    // Add this variable to the symbol table. <-- THIS IS THE NEW CALL
                    add_symbol(ctx, var_name, alloca_inst, var_type);

                    // If there's an initializer, process it and store the value.
                    if (initializer_expr_node)
                    {
                        LLVMValueRef initializer_value = process_expression(ctx, initializer_expr_node);
                        if (initializer_value)
                        {
                            LLVMBuildStore(ctx->builder, initializer_value, alloca_inst);
                        }
                        else
                        {
                            fprintf(stderr, "IRGen Error: Failed to process initializer for variable\n'%s'\n", var_name);
                        }
                    }
                }
                else
                {
                    fprintf(stderr, "IRGen Error: Could not extract variable name for declaration.\n");
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
        // Handle assignment like 'variable = expression'.
        // Children typically: [LHS_node, Operator_node, RHS_node].
        if (node->data.list.count >= 3 && node->data.list.children)
        {
            c_grammar_node_t *lhs_node = node->data.list.children[0]; // e.g., AST_NODE_IDENTIFIER.
            // Operator node (e.g., '=') is skipped for simplicity.
            c_grammar_node_t *rhs_node = node->data.list.children[2]; // The expression to assign.

            // Get the LLVM ValueRef for the variable's memory location (pointer).
            LLVMValueRef lhs_ptr = get_variable_pointer(ctx, lhs_node); // Needs symbol table lookup.
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
    case AST_NODE_RETURN_STATEMENT:
    {
        // Handle 'return expression;' or 'return;'.
        if (node->data.list.count > 0 && node->data.list.children)
        {
            // Process the return expression.
            c_grammar_node_t *expr_node = node->data.list.children[0];
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
        // --- Add cases for other AST_NODE types ---
        // Examples: AST_NODE_BINARY_OP, AST_NODE_UNARY_OP, AST_NODE_FUNCTION_CALL,
        // AST_NODE_IF_STATEMENT, AST_NODE_WHILE_STATEMENT, AST_NODE_FOR_STATEMENT, etc.
        // Each requires specific LLVM IR generation logic.

    default:
        // Fallback: Recursively process children for unhandled node types.
        // This is a basic strategy; specific nodes might need dedicated logic.
        if (node->data.list.children)
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
int write_llvm_ir_to_file(LLVMModuleRef module, const char *file_path)
{
    if (!module || !file_path)
    {
        fprintf(stderr, "IRGen Error: Invalid module or file path for writing IR.\n");
        return -1;
    }

    char *error_message = NULL;
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
 * @return The LLVM ValueRef (pointer) if found, NULL otherwise.
 */
static LLVMValueRef get_variable_pointer(ir_generator_ctx_t *ctx, c_grammar_node_t *identifier_node)
{
    if (!identifier_node || identifier_node->type != AST_NODE_IDENTIFIER ||
        !identifier_node->data.terminal.text)
    {
        fprintf(stderr, "IRGen Error: Invalid identifier node for get_variable_pointer.\n");
        return NULL;
    }
    const char *name = identifier_node->data.terminal.text;

    LLVMValueRef var_ptr;
    LLVMTypeRef var_type; // We might need type here later, e.g., for type checking.

    if (find_symbol(ctx, name, &var_ptr, &var_type))
    {
        return var_ptr;
    }
    else
    {
        fprintf(stderr, "IRGen Error: Undefined variable '%s' used.\n", name);
        return NULL;
    }
}

/**
 * @brief Processes an expression AST node and returns its LLVM ValueRef.
 * This function recursively handles various expression types.
 */
static LLVMValueRef process_expression(ir_generator_ctx_t *ctx, c_grammar_node_t *node)
{
    if (!node)
        return NULL;

    switch (node->type)
    {
    case AST_NODE_INTEGER_LITERAL:
    {
        // Handle integer literals like '42'.
        if (node->data.terminal.text)
        {
            long long value = strtoll(node->data.terminal.text, NULL, 0);
            // TODO: Determine the correct LLVM type based on context (e.g., declaration type).
            LLVMTypeRef int_type = LLVMInt32TypeInContext(ctx->context); // Assume int = i32.
            return LLVMConstInt(int_type, value, false);
        }
        break;
    }
    case AST_NODE_IDENTIFIER:
    {
        // When an identifier is used in an expression, we need to load its value.
        LLVMValueRef var_ptr = get_variable_pointer(ctx, node);
        if (var_ptr)
        {
            // Load the value from the memory address.
            return LLVMBuildLoad(ctx->builder, var_ptr, "load_tmp"); // "load_tmp" is a debug name.
        }
        else
        {
            // get_variable_pointer should handle printing the error.
            return NULL;
        }
        break;
    }
    case AST_NODE_BINARY_OP:
    {
        // Handle binary operations like '+', '-', '*', '/', etc.
        // AST structure: [Operator_node, LHS_node, RHS_node].
        if (node->data.list.count >= 3 && node->data.list.children)
        {
            c_grammar_node_t *op_node = node->data.list.children[0];
            c_grammar_node_t *lhs_node = node->data.list.children[1];
            c_grammar_node_t *rhs_node = node->data.list.children[2];

            LLVMValueRef lhs_val = process_expression(ctx, lhs_node);
            LLVMValueRef rhs_val = process_expression(ctx, rhs_node);

            if (lhs_val && rhs_val && op_node->data.terminal.text)
            {
                const char *op_str = op_node->data.terminal.text;
                // Map C operator strings to LLVM IR instructions.
                if (strcmp(op_str, "+") == 0)
                    return LLVMBuildAdd(ctx->builder, lhs_val, rhs_val, "add_tmp");
                if (strcmp(op_str, "-") == 0)
                    return LLVMBuildSub(ctx->builder, lhs_val, rhs_val, "sub_tmp");
                if (strcmp(op_str, "*") == 0)
                    return LLVMBuildMul(ctx->builder, lhs_val, rhs_val, "mul_tmp");
                // Add more operators as needed (division, modulo, bitwise, etc.).
                if (strcmp(op_str, "==") == 0)
                    return LLVMBuildICmp(ctx->builder, LLVMIntEQ, lhs_val, rhs_val, "eq_tmp"); // Corrected: LLVMIntEQ
                if (strcmp(op_str, "!=") == 0)
                    return LLVMBuildICmp(ctx->builder, LLVMIntNE, lhs_val, rhs_val, "ne_tmp"); // Corrected: LLVMIntNE
                if (strcmp(op_str, "<") == 0)
                    return LLVMBuildICmp(ctx->builder, LLVMIntSLT, lhs_val, rhs_val, "lt_tmp"); // Corrected: LLVMIntSLT
                if (strcmp(op_str, ">") == 0)
                    return LLVMBuildICmp(ctx->builder, LLVMIntSGT, lhs_val, rhs_val, "gt_tmp"); // Corrected: LLVMIntSGT
                // ... handle other comparisons like <=, >=
            }
            else
            {
                fprintf(stderr, "IRGen Error: Invalid operands or operator for binary operation.\n");
            }
        }
        break;
    }
    // TODO: Add cases for other expression types (unary ops, function calls, etc.).
    default:
        fprintf(stderr, "IRGen Warning: process_expression called on unexpected node type %d.\n", node->type);
        // Attempt to recursively process if it might yield a value.
        if (node->data.list.children)
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
