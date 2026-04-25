#pragma once

#include "c_grammar_ast.h"
#include "ir_gen_error.h"
#include "llvm_typed_value.h"
#include "scope.h"
#include "type_descriptors.h"

// Include necessary LLVM C API headers.
// These require LLVM to be installed and its include paths configured in CMake.
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h> // Potentially useful for JIT or engine operations
#include <llvm-c/Target.h>          // For target initialization, etc.
#include <llvm-c/TargetMachine.h>
#include <stdbool.h>

struct function_decl_entry
{
    char * name;
    TypedValue func;
    bool has_definition; // true if we've seen a function body
};

struct function_decls
{
    struct function_decl_entry * entries;
    size_t count;
    size_t capacity;
};

typedef struct
{
    bool generate_default_variables; /* Generate values for NULL etc when not preprocessing.*/
} ir_generation_flags;

typedef struct ref_types
{
    LLVMTypeRef i1_type;
    LLVMTypeRef i8_type;
    LLVMTypeRef i16_type;
    LLVMTypeRef i32_type;
    LLVMTypeRef i64_type;
    LLVMTypeRef ptr_type;
    LLVMTypeRef f32_type;
    LLVMTypeRef f64_type;
    LLVMTypeRef long_double_type;
    LLVMTypeRef void_type;
} ref_types;

// Structure to hold the context for LLVM IR generation.
// This includes LLVM's core objects and potentially symbol table management.
typedef struct ir_generator_ctx
{
    ir_generation_flags generation_flags;
    LLVMContextRef context;
    LLVMModuleRef module;
    LLVMBuilderRef builder;

    // --- Scope-based symbol table ---
    scope_t * current_scope; // Innermost active scope

    // --- Break target for switch and loops ---
    LLVMBasicBlockRef break_target;
    // --- Continue target for loops ---
    LLVMBasicBlockRef continue_target;

    // --- Error and warning collection ---
    ir_gen_error_collection_t errors;

    // --- Function declaration tracking ---
    struct function_decls function_declarations;

    ref_types ref_type;

    TypeDescriptors * type_descriptors;

    // Pseudo-code for your Compiler State
    int anon_counter;

} ir_generator_ctx_t;

/**
 * @brief Initializes the IR generator context.
 * This involves creating an LLVM context, module, and builder.
 * @return A pointer to the initialized ir_generator_ctx_t, or NULL on failure.
 */
ir_generator_ctx_t * ir_generator_init(char const * module_name, ir_generation_flags flags);

/**
 * @brief Generates LLVM IR from the provided Abstract Syntax Tree (AST).
 * The generation process populates the LLVM module within the context.
 * @param ctx The IR generator context.
 * @param ast_root The root node of the AST to process.
 * @return The LLVM module containing the generated IR, or NULL on failure.
 *         The caller should manage the lifetime of the returned module.
 */
LLVMModuleRef generate_llvm_ir(ir_generator_ctx_t * ctx, c_grammar_node_t const * ast_root);

/**
 * @brief Disposes of the IR generator context and associated LLVM resources.
 * @param ctx The IR generator context to dispose.
 */
void ir_generator_dispose(ir_generator_ctx_t * ctx);

/**
 * @brief Writes the LLVM IR module to a file in human-readable format.
 * @param module The LLVM module to write.
 * @param file_path The path to the output file.
 * @return 0 on success, -1 on failure.
 */
int write_llvm_ir_to_file(LLVMModuleRef module, char const * file_path);

/**
 * @brief Compiles the LLVM module to an object file or assembly file.
 * @param module The LLVM module to compile.
 * @param file_path The path to the output file.
 * @param march The target architecture (e.g., "x86-64").
 * @param file_type The type of file to emit (LLVMObjectFile or LLVMAssemblyFile).
 * @return 0 on success, -1 on failure.
 */
int emit_to_file(LLVMModuleRef module, char const * file_path, char const * march, LLVMCodeGenFileType file_type);
