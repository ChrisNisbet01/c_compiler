#include "enum_evaluation.h"

#include "generator_lists.h"

static int
evaluate_enum_value_assignment_expression(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * value_node, int current_value
)
{
    if (value_node == NULL)
    {
        debug_info("%s: value_node is NULL", __func__);
        return current_value;
    }

    debug_info(
        "%s: node type: %s (%d), current value: %d",
        __func__,
        get_node_type_name_from_node(value_node),
        value_node->type,
        current_value
    );

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

    switch (value_node->type)
    {
    case AST_NODE_INTEGER_LITERAL:
        current_value = (int)value_node->integer_lit.integer_literal.value;
        break;

    case AST_NODE_IDENTIFIER:
        if (value_node->text != NULL)
        {
            TypedValue symbol = NullTypedValue;
            if (generator_lookup_symbol_value(ctx, value_node->text, &symbol))
            {
                LLVMValueRef initializer = LLVMGetInitializer(symbol.value);
                if (initializer != NULL && LLVMIsAConstantInt(initializer))
                {
                    current_value = (int)LLVMConstIntGetZExtValue(initializer);
                }
            }
        }
        break;

    case AST_NODE_ARITHMETIC_EXPRESSION:
    {
        int lhs_value = evaluate_enum_value_assignment_expression(ctx, value_node->binary_expression.left, 0);
        int rhs_value = evaluate_enum_value_assignment_expression(ctx, value_node->binary_expression.right, 0);
        c_grammar_node_t const * op_node = value_node->binary_expression.op;
        arithmetic_operator_type_t op = op_node->op.arith.op;

        if (op == ARITH_OP_ADD)
        {
            current_value = lhs_value + rhs_value;
        }
        else if (op == ARITH_OP_SUB)
        {
            current_value = lhs_value - rhs_value;
        }
        else if (op == ARITH_OP_MUL)
        {
            current_value = lhs_value * rhs_value;
        }
        else if (op == ARITH_OP_DIV && rhs_value != 0)
        {
            current_value = lhs_value / rhs_value;
        }
        else if (op == ARITH_OP_MOD && rhs_value != 0)
        {
            current_value = lhs_value % rhs_value;
        }
    }
    break;

    case AST_NODE_BITWISE_EXPRESSION:
    {
        int lhs_value = evaluate_enum_value_assignment_expression(ctx, value_node->binary_expression.left, 0);
        int rhs_value = evaluate_enum_value_assignment_expression(ctx, value_node->binary_expression.right, 0);
        c_grammar_node_t const * op_node = value_node->binary_expression.op;
        bitwise_operator_type_t op = op_node->op.bitwise.op;

        switch (op)
        {
        case BITWISE_OP_AND:
            current_value = lhs_value & rhs_value;
            break;
        case BITWISE_OP_XOR:
            current_value = lhs_value ^ rhs_value;
            break;
        case BITWISE_OP_OR:
            current_value = lhs_value | rhs_value;
            break;
        default:
            break;
        }
    }
    break;

    case AST_NODE_SHIFT_EXPRESSION:
    {
        int lhs_value = evaluate_enum_value_assignment_expression(ctx, value_node->binary_expression.left, 0);
        int rhs_value = evaluate_enum_value_assignment_expression(ctx, value_node->binary_expression.right, 0);
        c_grammar_node_t const * op_node = value_node->binary_expression.op;
        shift_operator_type_t op = op_node->op.shift.op;

        if (op == SHIFT_OP_LL)
        {
            current_value = lhs_value << rhs_value;
        }
        else if (op == SHIFT_OP_AR)
        {
            current_value = lhs_value >> rhs_value;
        }
    }
    break;

    case AST_NODE_EQUALITY_EXPRESSION:
    {
        int lhs_value = evaluate_enum_value_assignment_expression(ctx, value_node->binary_expression.left, 0);
        int rhs_value = evaluate_enum_value_assignment_expression(ctx, value_node->binary_expression.right, 0);
        c_grammar_node_t const * op_node = value_node->binary_expression.op;
        equality_operator_type_t op = op_node->op.eq.op;

        if (op == EQ_OP_EQ)
        {
            current_value = (lhs_value == rhs_value) ? 1 : 0;
        }
        else if (op == EQ_OP_NE)
        {
            current_value = (lhs_value != rhs_value) ? 1 : 0;
        }
    }
    break;

    case AST_NODE_RELATIONAL_EXPRESSION:
    {
        int lhs_value = evaluate_enum_value_assignment_expression(ctx, value_node->binary_expression.left, 0);
        int rhs_value = evaluate_enum_value_assignment_expression(ctx, value_node->binary_expression.right, 0);
        c_grammar_node_t const * op_node = value_node->binary_expression.op;
        relational_operator_type_t op = op_node->op.rel.op;

        if (op == REL_OP_LT)
        {
            current_value = (lhs_value < rhs_value) ? 1 : 0;
        }
        else if (op == REL_OP_GT)
        {
            current_value = (lhs_value > rhs_value) ? 1 : 0;
        }
        else if (op == REL_OP_LE)
        {
            current_value = (lhs_value <= rhs_value) ? 1 : 0;
        }
        else if (op == REL_OP_GE)
        {
            current_value = (lhs_value >= rhs_value) ? 1 : 0;
        }
    }
    break;

    case AST_NODE_LOGICAL_EXPRESSION:
    {
        int lhs_value = evaluate_enum_value_assignment_expression(ctx, value_node->binary_expression.left, 0);
        int rhs_value = evaluate_enum_value_assignment_expression(ctx, value_node->binary_expression.right, 0);
        c_grammar_node_t const * op_node = value_node->binary_expression.op;
        logical_operator_type_t op = op_node->op.logical.op;

        if (op == LOGICAL_OP_AND)
        {
            current_value = (lhs_value && rhs_value) ? 1 : 0;
        }
        else if (op == LOGICAL_OP_OR)
        {
            current_value = (lhs_value || rhs_value) ? 1 : 0;
        }
    }
    break;

    case AST_NODE_UNARY_EXPRESSION_PREFIX:
    {
        c_grammar_node_t const * op_node = value_node->unary_expression_prefix.op;

        if (op_node == NULL)
        {
            debug_error("%s: op_node is NULL", __func__);
            return current_value;
        }
        if (op_node->op.unary.op != UNARY_OP_PLUS && op_node->op.unary.op != UNARY_OP_MINUS
            && op_node->op.unary.op != UNARY_OP_BITNOT)
        {
            debug_error("Unsupported unary operator: %d", op_node->op.unary.op);
            return current_value;
        }
        int next_value = evaluate_enum_value_assignment_expression(ctx, value_node->unary_expression_prefix.operand, 0);
        if (op_node->op.unary.op == UNARY_OP_PLUS)
        {
            current_value = next_value;
        }
        else if (op_node->op.unary.op == UNARY_OP_MINUS)
        {
            current_value = -next_value;
        }
        else if (op_node->op.unary.op == UNARY_OP_BITNOT)
        {
            current_value = ~next_value;
        }
        break;
    }

    default:
        break;
    }

#pragma GCC diagnostic pop

    return current_value;
}

bool
register_enum_constants(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node)
{
    if (ctx == NULL || enum_node == NULL || enum_node->type != AST_NODE_ENUM_DEFINITION)
    {
        return false;
    }

    debug_info("%s: node type: %s (%d)", __func__, get_node_type_name_from_node(enum_node), enum_node->type);

    c_grammar_node_t const * enumerator_list = enum_node->enum_definition.enumerator_list;

    int current_value = 0;

    for (size_t i = 0; i < enumerator_list->list.count; ++i)
    {
        c_grammar_node_t * child = enumerator_list->list.children[i];

        if (child->type == AST_NODE_ENUMERATOR)
        {
            c_grammar_node_t const * name_node = child->enumerator.identifier;
            c_grammar_node_t const * value_node = child->enumerator.expression;
            char const * enum_name = name_node->text;
            if (value_node != NULL)
            {
                current_value = evaluate_enum_value_assignment_expression(ctx, value_node, current_value);
            }

            TypeDescriptor const * enum_type = get_or_create_builtin_type(
                ctx->type_descriptors, (TypeSpecifier){.is_int = true}, (TypeQualifier){.is_const = true}
            );

            debug_info(
                "%s: registering: %s value: %d has expression: %d",
                __func__,
                enum_name,
                current_value,
                value_node != NULL
            );

            LLVMValueRef const_val = LLVMConstInt(enum_type->llvm_type, current_value, true);
            LLVMValueRef global = LLVMAddGlobal(ctx->module, enum_type->llvm_type, enum_name);
            LLVMSetInitializer(global, const_val);
            LLVMSetGlobalConstant(global, true);
            LLVMSetLinkage(global, LLVMInternalLinkage);

            TypedValue val = create_typed_value(global, enum_type, false);
            generator_add_symbol(ctx, enum_name, val);

            current_value++;
        }
    }

    return true;
}
