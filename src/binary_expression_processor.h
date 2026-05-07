#pragma once

#include "c_grammar_ast.h"
#include "llvm_ir_generator.h"
#include "typed_value.h"

#include <llvm-c/Core.h>

// Function pointer types for binary expression operations
typedef LLVMValueRef (*binary_arithmetic_int_operation_func_t)(LLVMBuilderRef builder, LLVMValueRef lhs, LLVMValueRef rhs, char const * name);
typedef LLVMValueRef (*binary_arithmetic_float_operation_func_t)(LLVMBuilderRef builder, LLVMValueRef lhs, LLVMValueRef rhs, char const * name);
typedef LLVMValueRef (*binary_comparison_int_operation_func_t)(LLVMBuilderRef builder, LLVMIntPredicate pred, LLVMValueRef lhs, LLVMValueRef rhs, char const * name);
typedef LLVMValueRef (*binary_comparison_float_operation_func_t)(LLVMBuilderRef builder, LLVMRealPredicate pred, LLVMValueRef lhs, LLVMValueRef rhs, char const * name);

// Structure to hold operation mappings
typedef struct {
    binary_arithmetic_int_operation_func_t int_arith_op;
    binary_arithmetic_float_operation_func_t float_arith_op;
    binary_comparison_int_operation_func_t int_cmp_op;
    binary_comparison_float_operation_func_t float_cmp_op;
    LLVMIntPredicate int_predicate;
    LLVMRealPredicate float_predicate;
    char const * name;
} binary_operation_mapping_t;

// Generic binary expression processor
TypedValue process_binary_expression(
    ir_generator_ctx_t * ctx,
    c_grammar_node_t const * node,
    binary_operation_mapping_t const * op_map,
    size_t op_map_size,
    char const * operation_name
);

// Specific operation mappings
extern binary_operation_mapping_t const bitwise_operations[];
extern size_t const bitwise_operations_count;

extern binary_operation_mapping_t const shift_operations[];
extern size_t const shift_operations_count;

extern binary_operation_mapping_t const arithmetic_operations[];
extern size_t const arithmetic_operations_count;

extern binary_operation_mapping_t const relational_operations[];
extern size_t const relational_operations_count;

extern binary_operation_mapping_t const equality_operations[];
extern size_t const equality_operations_count;

extern binary_operation_mapping_t const compound_assignment_operations[];
extern size_t const compound_assignment_operations_count;