#include "binary_expression_processor.h"

#include "debug.h"
#include "llvm_ir_generator.h"
#include "type_utils.h"

#include <stdlib.h>
#include <string.h>

// Function pointer types for binary expression operations
typedef LLVMValueRef (*binary_arithmetic_int_operation_func_t)(
    LLVMBuilderRef builder, LLVMValueRef lhs, LLVMValueRef rhs, char const * name
);
typedef LLVMValueRef (*binary_arithmetic_float_operation_func_t)(
    LLVMBuilderRef builder, LLVMValueRef lhs, LLVMValueRef rhs, char const * name
);
typedef LLVMValueRef (*binary_comparison_int_operation_func_t)(
    LLVMBuilderRef builder, LLVMIntPredicate pred, LLVMValueRef lhs, LLVMValueRef rhs, char const * name
);
typedef LLVMValueRef (*binary_comparison_float_operation_func_t)(
    LLVMBuilderRef builder, LLVMRealPredicate pred, LLVMValueRef lhs, LLVMValueRef rhs, char const * name
);

typedef struct
{
    binary_arithmetic_int_operation_func_t int_arith_op;
    binary_arithmetic_float_operation_func_t float_arith_op;
    binary_comparison_int_operation_func_t int_cmp_op;
    binary_comparison_float_operation_func_t float_cmp_op;
    LLVMIntPredicate int_predicate;
    LLVMRealPredicate float_predicate;
    char const * name;
} binary_operation_mapping_t;

typedef struct
{
    binary_operation_mapping_t const * operation_mapping_table;
    size_t operation_count; // Number of operations in the mapping tables
} binary_operation_details_t;

// Operation mappings
binary_operation_mapping_t const bitwise_operations[]
    = {{LLVMBuildAnd, NULL, NULL, NULL, 0, 0, "and_tmp"},
       {LLVMBuildOr, NULL, NULL, NULL, 0, 0, "or_tmp"},
       {LLVMBuildXor, NULL, NULL, NULL, 0, 0, "xor_tmp"}};

size_t const bitwise_operations_count = sizeof(bitwise_operations) / sizeof(binary_operation_mapping_t);

binary_operation_mapping_t const shift_operations[]
    = {{LLVMBuildShl, NULL, NULL, NULL, 0, 0, "shl_tmp"}, {LLVMBuildAShr, NULL, NULL, NULL, 0, 0, "ashr_tmp"}};

size_t const shift_operations_count = sizeof(shift_operations) / sizeof(binary_operation_mapping_t);

binary_operation_mapping_t const arithmetic_operations[]
    = {{LLVMBuildAdd, LLVMBuildFAdd, NULL, NULL, 0, 0, "arith_add_tmp"},
       {LLVMBuildSub, LLVMBuildFSub, NULL, NULL, 0, 0, "arith_sub_tmp"},
       {LLVMBuildMul, LLVMBuildFMul, NULL, NULL, 0, 0, "arith_mul_tmp"},
       {LLVMBuildSDiv, LLVMBuildFDiv, NULL, NULL, 0, 0, "arith_div_tmp"},
       {LLVMBuildSRem, NULL, NULL, NULL, 0, 0, "arith_rem_tmp"}};

size_t const arithmetic_operations_count = sizeof(arithmetic_operations) / sizeof(binary_operation_mapping_t);

binary_operation_mapping_t const relational_operations[]
    = {{NULL, NULL, LLVMBuildICmp, LLVMBuildFCmp, LLVMIntSLT, LLVMRealOLT, "flt_tmp"},
       {NULL, NULL, LLVMBuildICmp, LLVMBuildFCmp, LLVMIntSGT, LLVMRealOGT, "fgt_tmp"},
       {NULL, NULL, LLVMBuildICmp, LLVMBuildFCmp, LLVMIntSLE, LLVMRealOLE, "fle_tmp"},
       {NULL, NULL, LLVMBuildICmp, LLVMBuildFCmp, LLVMIntSGE, LLVMRealOGE, "fge_tmp"}};

size_t const relational_operations_count = sizeof(relational_operations) / sizeof(binary_operation_mapping_t);

binary_operation_mapping_t const equality_operations[]
    = {{NULL, NULL, LLVMBuildICmp, LLVMBuildFCmp, LLVMIntEQ, LLVMRealOEQ, "feq_tmp"},
       {NULL, NULL, LLVMBuildICmp, LLVMBuildFCmp, LLVMIntNE, LLVMRealONE, "fne_tmp"}};

size_t const equality_operations_count = sizeof(equality_operations) / sizeof(binary_operation_mapping_t);

binary_operation_mapping_t const compound_assignment_operations[]
    = {{LLVMBuildAdd, LLVMBuildFAdd, NULL, NULL, 0, 0, "fadd_tmp"},
       {LLVMBuildSub, LLVMBuildFSub, NULL, NULL, 0, 0, "fsub_tmp"},
       {LLVMBuildMul, LLVMBuildFMul, NULL, NULL, 0, 0, "fmul_tmp"},
       {LLVMBuildSDiv, LLVMBuildFDiv, NULL, NULL, 0, 0, "fdiv_tmp"},
       {LLVMBuildSRem, NULL, NULL, NULL, 0, 0, "rem_tmp"},
       {LLVMBuildAnd, NULL, NULL, NULL, 0, 0, "and_tmp"},
       {LLVMBuildOr, NULL, NULL, NULL, 0, 0, "or_tmp"},
       {LLVMBuildXor, NULL, NULL, NULL, 0, 0, "xor_tmp"},
       {LLVMBuildShl, NULL, NULL, NULL, 0, 0, "shl_tmp"},
       {LLVMBuildLShr, NULL, NULL, NULL, 0, 0, "lshr_tmp"}};

size_t const compound_assignment_operations_count
    = sizeof(compound_assignment_operations) / sizeof(binary_operation_mapping_t);

static binary_operation_details_t const binary_operation_details[BINARY_OP_COUNT] = {
    [BINARY_OP_BITWISE] = {bitwise_operations, bitwise_operations_count},
    [BINARY_OP_SHIFT] = {shift_operations, shift_operations_count},
    [BINARY_OP_ARITHMETIC] = {arithmetic_operations, arithmetic_operations_count},
    [BINARY_OP_RELATIONAL] = {relational_operations, relational_operations_count},
    [BINARY_OP_EQUALITY] = {equality_operations, equality_operations_count},
    [BINARY_OP_COMPOUND_ASSIGNMENT] = {compound_assignment_operations, compound_assignment_operations_count},
};

// Helper function to promote integer types
static void
promote_integer_operands(ir_generator_ctx_t * ctx, TypedValue * lhs_res, TypedValue * rhs_res)
{
    TypeDescriptor const * lhs_type = lhs_res->type_info;
    TypeDescriptor const * rhs_type = rhs_res->type_info;

    if (is_integer_kind(lhs_type) && is_integer_kind(rhs_type))
    {
        unsigned lhs_bits = LLVMGetIntTypeWidth(lhs_type->llvm_type);
        unsigned rhs_bits = LLVMGetIntTypeWidth(rhs_type->llvm_type);

        if (lhs_bits > rhs_bits)
        {
            rhs_res->value = LLVMBuildZExt(ctx->builder, rhs_res->value, lhs_type->llvm_type, "promote_rhs");
            rhs_res->type_info = lhs_type;
        }
        else if (rhs_bits > lhs_bits)
        {
            lhs_res->value = LLVMBuildZExt(ctx->builder, lhs_res->value, rhs_type->llvm_type, "promote_lhs");
            lhs_res->type_info = rhs_type;
        }
    }
}

// Helper function for signed integer promotion
static void
promote_signed_integer_operands(ir_generator_ctx_t * ctx, TypedValue * lhs_res, TypedValue * rhs_res)
{
    TypeDescriptor const * lhs_type = lhs_res->type_info;
    TypeDescriptor const * rhs_type = rhs_res->type_info;

    if (is_integer_kind(lhs_type) && is_integer_kind(rhs_type))
    {
        unsigned lhs_bits = LLVMGetIntTypeWidth(lhs_type->llvm_type);
        unsigned rhs_bits = LLVMGetIntTypeWidth(rhs_type->llvm_type);

        if (lhs_bits > rhs_bits)
        {
            rhs_res->value = LLVMBuildSExt(ctx->builder, rhs_res->value, lhs_type->llvm_type, "promote_rhs");
            rhs_res->type_info = lhs_type;
        }
        else if (rhs_bits > lhs_bits)
        {
            lhs_res->value = LLVMBuildSExt(ctx->builder, lhs_res->value, rhs_type->llvm_type, "promote_lhs");
            lhs_res->type_info = rhs_type;
        }
    }
}

TypedValue
process_binary_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node, binary_operation_type_t op_type)
{
    binary_operation_details_t const * details = &binary_operation_details[op_type];
    binary_operation_mapping_t const * op_map = details->operation_mapping_table;
    size_t op_map_size = details->operation_count;

    // Process LHS and RHS expressions
    TypedValue lhs_res = process_expression(ctx, node->binary_expression.left);
    if (lhs_res.value == NULL)
    {
        return NullTypedValue;
    }
    lhs_res = ensure_rvalue(ctx, "lhs_rval", lhs_res);

    TypedValue rhs_res = process_expression(ctx, node->binary_expression.right);
    if (rhs_res.value == NULL)
    {
        return NullTypedValue;
    }
    rhs_res = ensure_rvalue(ctx, "rhs_rval", rhs_res);

    // Promote operands as needed
    if (op_type == BINARY_OP_BITWISE || op_type == BINARY_OP_SHIFT)
    {
        promote_integer_operands(ctx, &lhs_res, &rhs_res);
    }
    else if (op_type == BINARY_OP_ARITHMETIC)
    {
        promote_signed_integer_operands(ctx, &lhs_res, &rhs_res);
    }
    // For relational and equality operations with integer operands, cast RHS to LHS type
    if ((op_type == BINARY_OP_RELATIONAL || op_type == BINARY_OP_EQUALITY) && is_integer_kind(lhs_res.type_info)
        && is_integer_kind(rhs_res.type_info))
    {
        rhs_res = cast_typed_value_to_desc(ctx, rhs_res, lhs_res.type_info);
    }

    // Determine if this is a floating point operation
    bool is_float_op = is_floating_kind(lhs_res.type_info) || is_floating_kind(rhs_res.type_info);

    // Get the operator
    c_grammar_node_t const * op_node = node->binary_expression.op;

    // Map the operator to an index in the operation map
    size_t op_index = 0;
    if (op_type == BINARY_OP_BITWISE)
    {
        bitwise_operator_type_t op = op_node->op.bitwise.op;
        op_index = (size_t)op;
    }
    else if (op_type == BINARY_OP_SHIFT)
    {
        shift_operator_type_t op = op_node->op.shift.op;
        op_index = (size_t)op;
    }
    else if (op_type == BINARY_OP_ARITHMETIC)
    {
        arithmetic_operator_type_t op = op_node->op.arith.op;
        op_index = (size_t)op;
    }
    else if (op_type == BINARY_OP_RELATIONAL)
    {
        relational_operator_type_t op = op_node->op.rel.op;
        op_index = (size_t)op;
    }
    else if (op_type == BINARY_OP_EQUALITY)
    {
        equality_operator_type_t op = op_node->op.eq.op;
        op_index = (size_t)op;
    }

    // Validate operator index
    if (op_index >= op_map_size)
    {
        debug_error("Invalid operator index %zu for %s operation", op_index, op_map->name);
        return NullTypedValue;
    }

    // Get the operation mapping
    binary_operation_mapping_t const * mapping = &op_map[op_index];

    // Perform the operation
    LLVMValueRef result_value = NULL;
    if (is_float_op && mapping->float_arith_op != NULL)
    {
        result_value = mapping->float_arith_op(ctx->builder, lhs_res.value, rhs_res.value, mapping->name);
    }
    else if (!is_float_op && mapping->int_arith_op != NULL)
    {
        result_value = mapping->int_arith_op(ctx->builder, lhs_res.value, rhs_res.value, mapping->name);
    }
    else if (is_float_op && mapping->float_cmp_op != NULL)
    {
        result_value = mapping->float_cmp_op(
            ctx->builder, mapping->float_predicate, lhs_res.value, rhs_res.value, mapping->name
        );
    }
    else if (!is_float_op && mapping->int_cmp_op != NULL)
    {
        result_value
            = mapping->int_cmp_op(ctx->builder, mapping->int_predicate, lhs_res.value, rhs_res.value, mapping->name);
    }
    else
    {
        debug_error("Unsupported operation for %s with float=%d", op_map->name, is_float_op);
        return NullTypedValue;
    }

    // For relational and equality operations, we need to return a boolean type
    if (op_type == BINARY_OP_RELATIONAL || op_type == BINARY_OP_EQUALITY)
    {
        TypeDescriptor const * bool_type
            = get_or_create_builtin_type(ctx->type_descriptors, (TypeSpecifier){.is_bool = true}, (TypeQualifier){0});
        return create_typed_value(result_value, bool_type, false);
    }

    // For other operations, return the result with the promoted type
    TypedValue result = lhs_res;
    result.value = result_value;
    result.is_lvalue = false;

    return result;
}