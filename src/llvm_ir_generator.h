#pragma once

#include "c_grammar_ast.h"

// Include necessary LLVM C API headers.
// These require LLVM to be installed and its include paths configured in CMake.
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h> // Potentially useful for JIT or engine operations
#include <llvm-c/Target.h>          // For target initialization, etc.

// Define LLVM types for convenience
typedef LLVMContextRef LLVMContextRef;
typedef LLVMModuleRef LLVMModuleRef;
typedef LLVMBuilderRef LLVMBuilderRef;
typedef LLVMValueRef LLVMValueRef;
typedef LLVMTypeRef LLVMTypeRef;

// Structure to hold the context for LLVM IR generation.
// This includes LLVM's core objects and potentially symbol table management.
typedef struct
{
    LLVMContextRef context;
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    // TODO: Add symbol table management for variables, functions, scopes, etc.
    // For now, this will be conceptual.
    // symbol_table_t* symbol_table;
} ir_generator_ctx_t;

/**
 * @brief Initializes the IR generator context.
 * This involves creating an LLVM context, module, and builder.
 * @return A pointer to the initialized ir_generator_ctx_t, or NULL on failure.
 */
ir_generator_ctx_t *ir_generator_init();

/**
 * @brief Generates LLVM IR from the provided Abstract Syntax Tree (AST).
 * The generation process populates the LLVM module within the context.
 * @param ctx The IR generator context.
 * @param ast_root The root node of the AST to process.
 * @return The LLVM module containing the generated IR, or NULL on failure.
 *         The caller should manage the lifetime of the returned module.
 */
LLVMModuleRef generate_llvm_ir(ir_generator_ctx_t *ctx, c_grammar_node_t const *ast_root);

/**
 * @brief Disposes of the IR generator context and associated LLVM resources.
 * @param ctx The IR generator context to dispose.
 */
void ir_generator_dispose(ir_generator_ctx_t *ctx);

/**
 * @brief Writes the LLVM IR module to a file in human-readable format.
 * @param module The LLVM module to write.
 * @param file_path The path to the output file.
 * @return 0 on success, -1 on failure.
 */
int write_llvm_ir_to_file(LLVMModuleRef module, const char *file_path);
