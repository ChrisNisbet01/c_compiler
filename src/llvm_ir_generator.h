#pragma once

#include "c_grammar_ast.h"

// Include necessary LLVM C API headers.
// These require LLVM to be installed and its include paths configured in CMake.
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h> // Potentially useful for JIT or engine operations
#include <llvm-c/Target.h>          // For target initialization, etc.
#include <llvm-c/TargetMachine.h>

// Define LLVM types for convenience
typedef LLVMContextRef LLVMContextRef;
typedef LLVMModuleRef LLVMModuleRef;
typedef LLVMBuilderRef LLVMBuilderRef;
typedef LLVMValueRef LLVMValueRef;
typedef LLVMTypeRef LLVMTypeRef;

// --- Struct type info for member access ---
typedef struct struct_field
{
    char *name;
    LLVMTypeRef type;
} struct_field_t;

typedef struct struct_info
{
    char *name;
    LLVMTypeRef type;
    struct_field_t *fields;
    size_t field_count;
} struct_info_t;

// --- Symbol Table Management ---
// Define symbol_t structure
typedef struct symbol
{
    char *name;
    LLVMValueRef ptr;
    LLVMTypeRef type;
    LLVMTypeRef pointee_type; // For pointer types, stores the pointed-to type (e.g., for int* this would be i32)
    char *struct_name; // For pointer-to-struct types, stores the struct name for member access
} symbol_t;

// --- Label Management ---
typedef struct label
{
    char *name;
    LLVMBasicBlockRef block;
} label_t;

// Structure to hold the context for LLVM IR generation.
// This includes LLVM's core objects and potentially symbol table management.
typedef struct ir_generator_ctx
{
    LLVMContextRef context;
    LLVMModuleRef module;
    LLVMBuilderRef builder;

    // --- Symbol table for local variables within a function scope ---
    symbol_t *symbol_table;
    size_t symbol_count;
    size_t symbol_capacity;

    // --- Struct type registry ---
    struct_info_t *structs;
    size_t struct_count;
    size_t struct_capacity;

    // --- Label management for goto statements ---
    label_t *labels;
    size_t label_count;
    size_t label_capacity;

    // --- Break target for switch and loops ---
    LLVMBasicBlockRef break_target;
    // --- Continue target for loops ---
    LLVMBasicBlockRef continue_target;

    // Potentially function/scope management structures
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

/**
 * @brief Compiles the LLVM module to an object file or assembly file.
 * @param module The LLVM module to compile.
 * @param file_path The path to the output file.
 * @param march The target architecture (e.g., "x86-64").
 * @param file_type The type of file to emit (LLVMObjectFile or LLVMAssemblyFile).
 * @return 0 on success, -1 on failure.
 */
int emit_to_file(LLVMModuleRef module, const char *file_path, const char *march, LLVMCodeGenFileType file_type);
