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
    char * name;
    LLVMTypeRef type;
    unsigned bit_offset;
    unsigned bit_width;     // bit_width == 0 indicates this is not a bitfield
    unsigned storage_index; // -1 for regular fields, >=0 for bitfields = index of storage field
} struct_field_t;

// --- Type kind for tagged types and typedef entries ---
typedef enum
{
    TYPE_KIND_UNKNOWN,         // Unassigned/unknown type kind
    TYPE_KIND_STRUCT,          // Tagged struct
    TYPE_KIND_UNION,           // Tagged union
    TYPE_KIND_UNTAGGED_STRUCT, // Untagged struct (anonymous)
    TYPE_KIND_UNTAGGED_UNION,  // Untagged union (anonymous)
    TYPE_KIND_ENUM,            // Tagged enum
    TYPE_KIND_UNTAGGED_ENUM    // Untagged enum (anonymous)
} type_kind_t;

typedef struct tagged_type_info
{
    char * name;
    type_kind_t kind; // TYPE_KIND_STRUCT, TYPE_KIND_UNION, or TYPE_KIND_ENUM
    LLVMTypeRef type;
    struct_field_t * fields;
    size_t field_count;
} tagged_type_info_t;

// --- Typedef entry ---
typedef struct scope_typedef_entry
{
    char * name;        // The typedef's own name
    type_kind_t kind;   // Which category this refers to
    LLVMTypeRef type;   // Only used for non-struct kinds (e.g., primitives)
    char * tag;         // For tagged kinds - which entry in struct/union list
    int untagged_index; // For untagged kinds - index into untagged list, -1 otherwise
} scope_typedef_entry_t;

// --- Tagged types (structs/unions/enums) in a scope ---
typedef struct scope_tagged_types
{
    tagged_type_info_t * entries;
    size_t count;
    size_t capacity;
} scope_tagged_types_t;

// --- Untagged structs/unions in a scope ---
typedef struct scope_untagged_structs
{
    LLVMTypeRef * types;
    size_t count;
    size_t capacity;
} scope_untagged_structs_t;

// --- Typedefs in a scope ---
typedef struct scope_typedefs
{
    scope_typedef_entry_t * entries;
    size_t count;
    size_t capacity;
} scope_typedefs_t;

// --- Symbol Table Management ---
// Define symbol_t structure
typedef struct symbol
{
    char * name;
    LLVMValueRef ptr;
    LLVMTypeRef type;
    LLVMTypeRef pointee_type; // For pointer types, stores the pointed-to type (e.g., for int* this would be i32)
    char * struct_name;       // For pointer-to-struct types, stores the struct name for member access
} symbol_t;

// --- Scope structure for hierarchical symbol tables ---
typedef struct scope
{
    symbol_t * symbols;
    size_t symbol_count;
    size_t symbol_capacity;

    scope_tagged_types_t tagged_types;         // Tagged struct/union/enum types
    scope_untagged_structs_t untagged_structs; // Anonymous structs/unions
    scope_typedefs_t typedefs;                 // Typedef names

    struct scope * parent; // Chain to outer scope (NULL for global)
} scope_t;

// --- Label Management ---
typedef struct label
{
    char * name;
    LLVMBasicBlockRef block;
} label_t;

// Structure to hold the context for LLVM IR generation.
// This includes LLVM's core objects and potentially symbol table management.
typedef struct ir_generator_ctx
{
    LLVMContextRef context;
    LLVMModuleRef module;
    LLVMBuilderRef builder;

    // --- Scope-based symbol table ---
    scope_t * current_scope; // Innermost active scope

    // --- Label management for goto statements ---
    label_t * labels;
    size_t label_count;
    size_t label_capacity;

    // --- Break target for switch and loops ---
    LLVMBasicBlockRef break_target;
    // --- Continue target for loops ---
    LLVMBasicBlockRef continue_target;
} ir_generator_ctx_t;

/**
 * @brief Initializes the IR generator context.
 * This involves creating an LLVM context, module, and builder.
 * @return A pointer to the initialized ir_generator_ctx_t, or NULL on failure.
 */
ir_generator_ctx_t * ir_generator_init(void);

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
