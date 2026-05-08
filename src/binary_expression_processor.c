#include "binary_expression_processor.h"

#include "debug.h"
#include "ir_utils.h"
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
    bool return_boolean;
} binary_operation_details_t;

// Operation mappings
static binary_operation_mapping_t const bitwise_operations[]
    = {[BITWISE_OP_AND] = {
           .int_arith_op = LLVMBuildAnd,
           .name = "and_tmp",
       },
       [BITWISE_OP_OR] = {
           .int_arith_op = LLVMBuildOr,
           .name = "or_tmp",
       },
       [BITWISE_OP_XOR] = {
           .int_arith_op = LLVMBuildXor,
           .name = "xor_tmp",
       }};

static binary_operation_mapping_t const shift_operations[]
    = {[SHIFT_OP_LL] = {
           .int_arith_op = LLVMBuildShl,
           .name = "shl_tmp",
       },
       [SHIFT_OP_AR] = {
           LLVMBuildAShr,
           .name = "ashr_tmp",
       }};

static binary_operation_mapping_t const arithmetic_operations[]
    = {[ARITH_OP_ADD] = {
           .int_arith_op = LLVMBuildAdd,
           .float_arith_op = LLVMBuildFAdd,
           .name = "arith_add_tmp",
       },
       [ARITH_OP_SUB] = {
           .int_arith_op = LLVMBuildSub,
           .float_arith_op = LLVMBuildFSub,
           .name = "arith_sub_tmp",
       },
       [ARITH_OP_MUL] = {
           .int_arith_op = LLVMBuildMul,
           .float_arith_op = LLVMBuildFMul,
           .name = "arith_mul_tmp",
       },
       [ARITH_OP_DIV] = {
           .int_arith_op = LLVMBuildSDiv,
           .float_arith_op = LLVMBuildFDiv,
           .name = "arith_div_tmp",
       },
       [ARITH_OP_MOD] = {
           .int_arith_op = LLVMBuildSRem,
           .name = "arith_rem_tmp",
       }};

static binary_operation_mapping_t const relational_operations[]
    = {[REL_OP_LT] = {
           .int_cmp_op = LLVMBuildICmp,
           .float_cmp_op = LLVMBuildFCmp,
           .int_predicate = LLVMIntSLT,
           .float_predicate = LLVMRealOLT,
           .name = "flt_tmp",
       },
       [REL_OP_GT] = {
           .int_cmp_op = LLVMBuildICmp,
           .float_cmp_op = LLVMBuildFCmp,
           .int_predicate = LLVMIntSGT,
           .float_predicate = LLVMRealOGT,
           .name = "fgt_tmp",
       },
       [REL_OP_LE] = {
           .int_cmp_op = LLVMBuildICmp,
           .float_cmp_op = LLVMBuildFCmp,
           .int_predicate = LLVMIntSLE,
           .float_predicate = LLVMRealOLE,
           .name = "fle_tmp",
       },
       [REL_OP_GE] = {
           .int_cmp_op = LLVMBuildICmp,
           .float_cmp_op = LLVMBuildFCmp,
           .int_predicate = LLVMIntSGE,
           .float_predicate = LLVMRealOGE,
           .name = "fge_tmp",
       }};

static binary_operation_mapping_t const equality_operations[]
    = {[EQ_OP_EQ] = {
           .int_cmp_op = LLVMBuildICmp,
           .float_cmp_op = LLVMBuildFCmp,
           .int_predicate = LLVMIntEQ,
           .float_predicate = LLVMRealOEQ,
           .name = "feq_tmp",
       },
       [EQ_OP_NE] = {
           .int_cmp_op = LLVMBuildICmp,
           .float_cmp_op = LLVMBuildFCmp,
           .int_predicate = LLVMIntNE,
           .float_predicate = LLVMRealONE,
           .name = "fne_tmp",
       }};

static binary_operation_mapping_t const compound_assignment_operations[]
    = {[ASSIGN_OP_ADD] = {
           .int_arith_op = LLVMBuildAdd,
           .float_arith_op = LLVMBuildFAdd,
           .name = "fadd_tmp",
       },
       [ASSIGN_OP_SUB] = {
           .int_arith_op = LLVMBuildSub,
           .float_arith_op = LLVMBuildFSub,
           .name = "fsub_tmp",
       },
       [ASSIGN_OP_MUL] = {
           .int_arith_op = LLVMBuildMul,
           .float_arith_op = LLVMBuildFMul,
           .name = "fmul_tmp",
       },
       [ASSIGN_OP_DIV] = {
           .int_arith_op = LLVMBuildSDiv,
           .float_arith_op = LLVMBuildFDiv,
           .name = "fdiv_tmp",
       },
       [ASSIGN_OP_MOD] = {
           .int_arith_op = LLVMBuildSRem,
           .name = "rem_tmp",
       },
       [ASSIGN_OP_AND] = {
           .int_arith_op = LLVMBuildAnd,
           .name = "and_tmp",
       },
       [ASSIGN_OP_OR] = {
           .int_arith_op = LLVMBuildOr,
           .name = "or_tmp",
       },
       [ASSIGN_OP_XOR] = {
           .int_arith_op = LLVMBuildXor,
           .name = "xor_tmp",
       },
       [ASSIGN_OP_SHL] = {
           .int_arith_op = LLVMBuildShl,
           .name = "shl_tmp",
       },
       [ASSIGN_OP_SHR] = {
           .int_arith_op = LLVMBuildLShr,
           .name = "lshr_tmp",
       }};

static binary_operation_details_t const binary_operation_details[BINARY_OP_COUNT] = {
    [BINARY_OP_BITWISE] = {
        .operation_mapping_table = bitwise_operations, 
        .operation_count = ARRAY_SIZE(bitwise_operations), 
        .return_boolean = false,
    },
    [BINARY_OP_SHIFT] = {
        .operation_mapping_table = shift_operations, 
        .operation_count = ARRAY_SIZE(shift_operations), 
        .return_boolean = false,
    },
    [BINARY_OP_ARITHMETIC] = {
        .operation_mapping_table = arithmetic_operations, 
        .operation_count = ARRAY_SIZE(arithmetic_operations), 
        .return_boolean = false,
    },
    [BINARY_OP_RELATIONAL] = {
        .operation_mapping_table = relational_operations, 
        .operation_count = ARRAY_SIZE(relational_operations), 
        .return_boolean = true,
    },
    [BINARY_OP_EQUALITY] = {
        .operation_mapping_table = equality_operations, 
        .operation_count = ARRAY_SIZE(equality_operations), 
        .return_boolean = true,
    },
    [BINARY_OP_COMPOUND_ASSIGNMENT] = {
        .operation_mapping_table = compound_assignment_operations, 
        .operation_count = ARRAY_SIZE(compound_assignment_operations), 
        .return_boolean = false,
    },
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
complete_binary_expression(
    ir_generator_ctx_t * ctx, TypedValue lhs_res, c_grammar_node_t const * node, binary_operation_type_t op_type
)
{
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

    binary_operation_details_t const * details = &binary_operation_details[op_type];
    binary_operation_mapping_t const * op_map = details->operation_mapping_table;
    size_t op_map_size = details->operation_count;

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
    else if (op_type == BINARY_OP_COMPOUND_ASSIGNMENT)
    {
        assignment_operator_type_t op = op_node->op.assign.op;
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

    // Handle pointer arithmetic: ptr +/- integer (requires GEP, not add/sub)
    if ((op_type == BINARY_OP_ARITHMETIC || op_type == BINARY_OP_COMPOUND_ASSIGNMENT)
        && (op_index == ARITH_OP_ADD || op_index == ARITH_OP_SUB || op_index == ASSIGN_OP_ADD
            || op_index == ASSIGN_OP_SUB))
    {
        TypeDescriptor const * ptr_type = NULL;
        LLVMValueRef ptr_val = NULL;
        LLVMValueRef idx_val = NULL;

        if (lhs_res.type_info != NULL && lhs_res.type_info->kind == NCC_TYPE_KIND_POINTER && rhs_res.type_info != NULL
            && is_integer_kind(rhs_res.type_info))
        {
            ptr_type = lhs_res.type_info;
            ptr_val = lhs_res.value;
            idx_val = rhs_res.value;
        }
        else if (
            rhs_res.type_info != NULL && rhs_res.type_info->kind == NCC_TYPE_KIND_POINTER && lhs_res.type_info != NULL
            && is_integer_kind(lhs_res.type_info) && op_index == ARITH_OP_ADD
        )
        {
            ptr_type = rhs_res.type_info;
            ptr_val = rhs_res.value;
            idx_val = lhs_res.value;
        }

        if (ptr_type != NULL)
        {
            if (op_index == ARITH_OP_SUB || op_index == ASSIGN_OP_SUB)
            {
                idx_val = LLVMBuildNeg(ctx->builder, idx_val, "neg_idx");
            }
            result_value = LLVMBuildInBoundsGEP2(
                ctx->builder, ptr_type->pointee->llvm_type, ptr_val, &idx_val, 1, "ptr_add_tmp"
            );
            // Return result with the pointer type
            TypedValue result = create_typed_value(result_value, ptr_type, false);
            return result;
        }
    }

    // Handle pointer subtraction: ptr - ptr (requires ptrtoint + sub + div by element size)
    if (op_type == BINARY_OP_ARITHMETIC && op_index == ARITH_OP_SUB && lhs_res.type_info != NULL
        && lhs_res.type_info->kind == NCC_TYPE_KIND_POINTER && rhs_res.type_info != NULL
        && rhs_res.type_info->kind == NCC_TYPE_KIND_POINTER)
    {
        LLVMTypeRef int64_type = LLVMInt64TypeInContext(ctx->context);
        LLVMValueRef lhs_int = LLVMBuildPtrToInt(ctx->builder, lhs_res.value, int64_type, "ptr_sub_lhs");
        LLVMValueRef rhs_int = LLVMBuildPtrToInt(ctx->builder, rhs_res.value, int64_type, "ptr_sub_rhs");
        LLVMValueRef diff = LLVMBuildSub(ctx->builder, lhs_int, rhs_int, "ptr_sub_diff");

        if (lhs_res.type_info->pointee != NULL)
        {
            uint64_t pointee_size = get_type_size(ctx, lhs_res.type_info->pointee);
            if (pointee_size > 1)
            {
                LLVMValueRef size_val = LLVMConstInt(int64_type, pointee_size, false);
                diff = LLVMBuildSDiv(ctx->builder, diff, size_val, "ptr_sub_elems");
            }
        }

        result_value = LLVMBuildIntToPtr(ctx->builder, diff, lhs_res.type_info->llvm_type, "ptr_sub_result");
        TypedValue result = create_typed_value(result_value, lhs_res.type_info, false);
        return result;
    }

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

    if (details->return_boolean)
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

TypedValue
process_binary_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node, binary_operation_type_t op_type)
{
    // Process LHS and RHS expressions
    TypedValue lhs_res = process_expression(ctx, node->binary_expression.left);
    if (lhs_res.value == NULL)
    {
        return NullTypedValue;
    }
    return complete_binary_expression(ctx, lhs_res, node, op_type);
}
