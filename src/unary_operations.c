#include "unary_operations.h"

#include "ast_node_name.h"
#include "c_grammar_ast.h"
#include "compound_literal_processor.h"
#include "debug.h"
#include "ir_utils.h"
#include "llvm_ir_generator.h"
#include "typed_value.h"

#include <llvm-c/Core.h>

TypedValue
process_unary_expression_prefix(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Unary structure: [Operator, Operand]
    c_grammar_node_t const * op = node->unary_expression_prefix.op;
    c_grammar_node_t const * operand_node = node->unary_expression_prefix.operand;

    switch (op->op.unary.op)
    {
    case UNARY_OP_ADDR:
    {
        TypedValue v;
        if (operand_node->type == AST_NODE_COMPOUND_LITERAL)
        {
            v = process_compound_literal(ctx, operand_node);
        }
        else
        {
            v = process_expression(ctx, operand_node);
        }

        if (v.value == NULL)
        {
            return NullTypedValue;
        }

        // 1. Safety Check: You can only take the address of something in memory
        if (!v.is_lvalue)
        {
            // If it's an r-value (like a function return), we must spill it to
            // the stack first to get an address.
            v = ensure_lvalue(ctx, "addr_tmp", v);
        }

        // 2. The result of '&' is an r-value (the pointer itself)
        // 3. The type must be transformed from 'T' to 'T*'
        TypeDescriptor const * ptr_type
            = get_or_create_pointer_type(ctx->type_descriptors, v.type_info, (TypeQualifier){0});

        // v.value already contains the address (because is_lvalue was true)
        return create_typed_value(v.value, ptr_type, false);
    }

    case UNARY_OP_DEREF:
    {
        TypedValue operand_res = process_expression(ctx, operand_node);
        if (operand_res.value == NULL)
        {
            return NullTypedValue;
        }

        // 1. Validation: Must be a pointer
        if (operand_res.type_info->kind != NCC_TYPE_KIND_POINTER)
        {
            ir_gen_error(&ctx->errors, operand_node, "Dereference operand is not a pointer");
            return NullTypedValue;
        }

        // 2. Extract the actual address
        // If the operand is an l-value (pointer to a pointer), we need to load it
        // to get the actual pointer value we want to dereference.
        operand_res = ensure_rvalue(ctx, "deref_addr", operand_res);

        // 3. Transform Type: Pointer to T -> T
        if (!typed_value_switch_to_pointee(&operand_res))
        {
            ir_gen_error(&ctx->errors, operand_node, "Failed to resolve pointee type");
            return NullTypedValue;
        }

        // 4. Set state: The 'value' is the address, and it's now an l-value
        operand_res.is_lvalue = true;

        // Crucial: If your system uses bitfields or special metadata for structs,
        // ensure they are cleared or updated for the new type.
        operand_res.bitfield.bit_width = 0;

        return operand_res;
    }

    case UNARY_OP_MINUS:
    {
        TypedValue operand_res = process_expression(ctx, operand_node);
        if (operand_res.value == NULL)
        {
            debug_error("Operand processing failed for unary minus");
            return NullTypedValue;
        }
        operand_res = ensure_rvalue(ctx, "unary_op_minus_rval", operand_res);

        if (is_floating_kind(operand_res.type_info))
        {
            operand_res.value = LLVMBuildFNeg(ctx->builder, operand_res.value, "fneg_tmp");
        }
        else
        {
            operand_res.value = LLVMBuildNeg(ctx->builder, operand_res.value, "neg_tmp");
        }
        operand_res.is_lvalue = false;

        return operand_res;
    }

    case UNARY_OP_NOT:
    {
        TypedValue operand_res = process_expression(ctx, operand_node);
        if (operand_res.value == NULL)
        {
            debug_error("Operand processing failed for unary not");
            return NullTypedValue;
        }
        operand_res = ensure_rvalue(ctx, "un_not_rval", operand_res);

        // 1. Comparison produces an i1 (1-bit integer)
        LLVMValueRef zero = LLVMConstNull(operand_res.type_info->llvm_type);
        LLVMValueRef is_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, operand_res.value, zero, "is_zero_tmp");
        TypeDescriptor const * bool_desc = type_descriptor_get_bool_type(ctx->type_descriptors, false);

        return create_typed_value(is_zero, bool_desc, false);
    }

    case UNARY_OP_BITNOT:
    {
        TypedValue operand_res = process_expression(ctx, operand_node);
        if (operand_res.value == NULL)
        {
            debug_error("Operand processing failed for unary bitnot");
            return NullTypedValue;
        }
        operand_res = ensure_rvalue(ctx, "bit_not_rval", operand_res);

        operand_res.value = LLVMBuildNot(ctx->builder, operand_res.value, "bitnot_tmp");
        operand_res.is_lvalue = false;
        return operand_res;
    }

    case UNARY_OP_INC:
    case UNARY_OP_DEC:
    {
        TypedValue var_res = process_expression(ctx, operand_node);

        if (var_res.value == NULL)
        {
            return NullTypedValue;
        }

        if (!var_res.is_lvalue)
        {
            ir_gen_error(&ctx->errors, operand_node, "Cannot increment/decrement non-lvalue");
            return NullTypedValue;
        }

        if (var_res.type_info->qualifiers.is_const)
        {
            ir_gen_error(&ctx->errors, operand_node, "Cannot increment/decrement const variable");
            return NullTypedValue;
        }

        TypedValue rvalue_res = ensure_rvalue(ctx, "unary_inc_dev_rval", var_res);
        LLVMValueRef original_val = rvalue_res.value;
        TypeDescriptor const * one_type = type_descriptor_get_int32_type(ctx->type_descriptors, false);
        LLVMValueRef one = LLVMConstInt(one_type->llvm_type, 1, false);
        TypedValue one_val = create_typed_value(one, one_type, false);

        one_val = cast_typed_value_to_desc(ctx, one_val, rvalue_res.type_info);

        LLVMValueRef new_val;
        if (op->op.unary.op == UNARY_OP_INC)
        {
            if (is_floating_kind(var_res.type_info))
                new_val = LLVMBuildFAdd(
                    ctx->builder, original_val, LLVMConstReal(var_res.type_info->llvm_type, 1.0), "inc_tmp"
                );
            else
                new_val = LLVMBuildAdd(ctx->builder, original_val, one_val.value, "inc_tmp");
        }
        else
        {
            if (is_floating_kind(var_res.type_info))
                new_val = LLVMBuildFSub(
                    ctx->builder, original_val, LLVMConstReal(var_res.type_info->llvm_type, 1.0), "dec_tmp"
                );
            else
                new_val = LLVMBuildSub(ctx->builder, original_val, one_val.value, "dec_tmp");
        }
        aligned_store(ctx, ctx->builder, new_val, var_res.type_info->llvm_type, var_res.value);
        var_res.value = new_val;
        var_res.is_lvalue = false;

        return var_res;
    }

    case UNARY_OP_PLUS:
    {
        TypedValue var_res = process_expression(ctx, operand_node);
        if (var_res.value == NULL)
        {
            debug_error("Operand processing failed for unary unary");
            return NullTypedValue;
        }
        var_res.is_lvalue = false;

        return var_res;
    }

    case UNARY_OP_SIZEOF:
    {
        TypeDescriptor const * target_type = NULL;

        // Check if operand is a TypeName (e.g., sizeof(int) or sizeof(struct Point))
        if (operand_node->type == AST_NODE_TYPE_NAME)
        {
            // TypeName contains TypeSpecifier(s), possibly with struct/union keyword
            target_type = get_type_descriptor_from_type_name(ctx, operand_node);
        }
        else
        {
            TypedValue operand_res = process_expression(ctx, operand_node);
            if (operand_res.value == NULL)
            {
                debug_error("Operand processing failed for unary sizeof");
                return NullTypedValue;
            }

            target_type = operand_res.type_info;
        }

        uint64_t sizeof_type_bytes = get_type_size(ctx, target_type);
        TypeDescriptor const * sizeof_desc = type_descriptor_get_uint64_type(ctx->type_descriptors, true);
        LLVMValueRef val = LLVMConstInt(sizeof_desc->llvm_type, sizeof_type_bytes, false);

        return create_typed_value(val, sizeof_desc, false);
    }
    case UNARY_OP_ALIGNOF:
    {
        // alignof is similar to sizeof but returns alignment
        TypeDescriptor const * target_type = NULL;

        // Handle TypeName (e.g., alignof(int) or alignof(struct Point))
        if (operand_node->type == AST_NODE_TYPE_NAME)
        {
            target_type = get_type_descriptor_from_type_name(ctx, operand_node);
        }
        else
        {
            TypedValue operand_res = process_expression(ctx, operand_node);

            if (operand_res.value == NULL)
            {
                debug_error("Operand processing failed for unary alignof");
                return NullTypedValue;
            }

            target_type = operand_res.type_info;
        }

        uint64_t alignment = get_type_alignment_desc(ctx->data_layout, target_type);
        TypeDescriptor const * alignment_desc = type_descriptor_get_uint64_type(ctx->type_descriptors, true);
        LLVMValueRef val = LLVMConstInt(alignment_desc->llvm_type, alignment, false);

        return create_typed_value(val, alignment_desc, false);
    }

    case UNARY_OP_OFFSETOF:
    {
        TypeDescriptor const * target_type = NULL;

        // Handle TypeName (e.g., alignof(int) or alignof(struct Point))
        if (operand_node->type != AST_NODE_TYPE_NAME)
        {
            debug_error(
                "%s: Offsetof expected %s, but got %s",
                __func__,
                get_node_type_name_from_type(AST_NODE_TYPE_NAME),
                get_node_type_name_from_node(operand_node)
            );

            return NullTypedValue;
        }

        target_type = get_type_descriptor_from_type_name(ctx, operand_node);

        // 1. Validation: Must be a struct or union.
        if (target_type->kind != NCC_TYPE_KIND_STRUCT && target_type->kind != NCC_TYPE_KIND_UNION)
        {
            ir_gen_error(&ctx->errors, operand_node, "Offsetof only applies to structs and unions");

            return NullTypedValue;
        }

        char const * member_name = node->unary_expression_prefix.operand2->text;
        int64_t offset = get_type_member_offset_desc(target_type, member_name);
        TypeDescriptor const * offset_desc = type_descriptor_get_uint64_type(ctx->type_descriptors, true);
        LLVMValueRef val = LLVMConstInt(offset_desc->llvm_type, offset, false);

        return create_typed_value(val, offset_desc, false);
    }

    default:
    {
        debug_error("Unknown unary operator %u.", op->op.unary.op);

        return NullTypedValue;
    }
    }

    return NullTypedValue; /* Shouldn't happen. */
}
