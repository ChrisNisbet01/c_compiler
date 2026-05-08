#include "unary_operations.h"

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
        // For &compound_literal, we need to create a pointer to the temp
        // The compound literal code returns a loaded value, but we need the pointer
        if (operand_node->type == AST_NODE_COMPOUND_LITERAL)
        {
            TypedValue v = process_compound_literal(ctx, operand_node);

            if (v.value == NULL)
            {
                ir_gen_error(&ctx->errors, operand_node, "Cannot take the address of an rvalue");
                return NullTypedValue;
            }

            // --- The Bridge Logic ---
            TypeDescriptor const * base_desc = v.type_info;

            if (base_desc == NULL)
            {
                debug_error("No type descriptor found for compound literal, attempting fallback");
                return NullTypedValue;
            }

            v = create_typed_value(
                v.value, get_or_create_pointer_type(ctx->type_descriptors, base_desc, (TypeQualifier){0}), false
            );

            return v;
        }
        TypedValue v = process_expression(ctx, operand_node);
        if (v.value == NULL)
        {
            return NullTypedValue;
        }

        v.is_lvalue = false;
        // For &member or &array[i], process the expression which returns a pointer
        return v;
    }

    case UNARY_OP_DEREF:
    {
        TypedValue operand_res = process_expression(ctx, operand_node);
        if (operand_res.value == NULL)
        {
            debug_info("operand dereference failed");
            return NullTypedValue;
        }

        if (operand_res.type_info->kind != NCC_TYPE_KIND_POINTER)
        {
            ir_gen_error(
                &ctx->errors,
                operand_node,
                "Error: Dereference operand is not a pointer (value: %p)\n",
                (void *)operand_res.value
            );
            return NullTypedValue;
        }
        operand_res = ensure_rvalue(ctx, "un_op_deref", operand_res);
        if (!typed_value_switch_to_pointee(&operand_res))
        {
            ir_gen_error(&ctx->errors, operand_node, "Error: Failed to switch to pointee type for dereference");
            return NullTypedValue;
        }
        operand_res.is_lvalue = true;

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
        LLVMValueRef one = LLVMConstInt(ctx->ref_type.i32_type, 1, false);

        LLVMValueRef new_val;
        if (op->op.unary.op == UNARY_OP_INC)
        {
            if (is_floating_kind(var_res.type_info))
                new_val = LLVMBuildFAdd(
                    ctx->builder, original_val, LLVMConstReal(var_res.type_info->llvm_type, 1.0), "inc_tmp"
                );
            else
                new_val = LLVMBuildAdd(ctx->builder, original_val, one, "inc_tmp");
        }
        else
        {
            if (is_floating_kind(var_res.type_info))
                new_val = LLVMBuildFSub(
                    ctx->builder, original_val, LLVMConstReal(var_res.type_info->llvm_type, 1.0), "dec_tmp"
                );
            else
                new_val = LLVMBuildSub(ctx->builder, original_val, one, "dec_tmp");
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
            c_grammar_node_t const * qualifier_list = operand_node->type_name.specifier_qualifier_list;
            target_type = get_type_descriptor_from_specifier_list(ctx, qualifier_list);
        }
        else
        {
            debug_info("Operand node type for sizeof: %s", get_node_type_name_from_node(operand_node));
            TypedValue operand_res = process_expression(ctx, operand_node);
            if (operand_res.value == NULL)
            {
                debug_error("Operand processing failed for unary sizeof");
                return NullTypedValue;
            }

            target_type = operand_res.type_info;
        }
        debug_info("unary operator getting size of type: %p", target_type);
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
            c_grammar_node_t const * qualifier_list = operand_node->type_name.specifier_qualifier_list;
            target_type = get_type_descriptor_from_specifier_list(ctx, qualifier_list);
        }
        else
        {
            debug_info("Operand node type for alignof: %s", get_node_type_name_from_node(operand_node));
            TypedValue operand_res = process_expression(ctx, operand_node);
            if (operand_res.value == NULL)
            {
                debug_error("Operand processing failed for unary alignof");
                return NullTypedValue;
            }

            target_type = operand_res.type_info;
        }
        debug_info("unary operator getting alignment");
        uint64_t alignment = get_type_alignment_desc(target_type);
        TypeDescriptor const * alignment_desc = type_descriptor_get_uint64_type(ctx->type_descriptors, true);
        LLVMValueRef val = LLVMConstInt(alignment_desc->llvm_type, alignment, false);

        return create_typed_value(val, alignment_desc, false);
    }
    default:
    {
        debug_error("Unknown unary operator %u.", op->op.unary.op);
        return NullTypedValue;
    }
    }

    return NullTypedValue; /* Shouldn't happen. */
}
