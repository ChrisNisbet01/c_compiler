#include "type_utils.h"

#include "ast_node_name.h"
#include "debug.h"
#include "scope.h"
#include "type_descriptors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char const *
extract_struct_or_union_or_enum_tag(c_grammar_node_t const * type_spec_node)
{
    if (type_spec_node == NULL)
    {
        return NULL;
    }

    if (type_spec_node->type == AST_NODE_STRUCT_TYPE_REF || type_spec_node->type == AST_NODE_UNION_TYPE_REF
        || type_spec_node->type == AST_NODE_ENUM_TYPE_REF)
    {
        c_grammar_node_t const * ident = type_spec_node->type_ref.identifier;
        return ident ? ident->text : NULL;
    }

    if (type_spec_node->type != AST_NODE_TYPE_SPECIFIER || type_spec_node->list.count == 0)
    {
        return NULL;
    }

    c_grammar_node_t const * spec_child = type_spec_node->list.children[0];
    c_grammar_node_t const * id_node = NULL;

    if (spec_child->type == AST_NODE_ENUM_DEFINITION)
    {
        id_node = spec_child->enum_definition.identifier;
    }
    else if (spec_child->type == AST_NODE_STRUCT_DEFINITION || spec_child->type == AST_NODE_UNION_DEFINITION)
    {
        id_node = spec_child->struct_definition.identifier;
    }
    else if (
        spec_child->type == AST_NODE_ENUM_TYPE_REF || spec_child->type == AST_NODE_STRUCT_TYPE_REF
        || spec_child->type == AST_NODE_UNION_TYPE_REF
    )
    {
        id_node = spec_child->type_ref.identifier;
    }

    if (id_node == NULL)
    {
        return NULL;
    }

    return id_node->text;
}

char const *
extract_typedef_name(c_grammar_node_t const * type_spec_node)
{
    if (type_spec_node == NULL || type_spec_node->type != AST_NODE_TYPEDEF_SPECIFIER
        || type_spec_node->identifier.identifier == NULL)
    {
        debug_info("%s: no name", __func__);
        return NULL;
    }
    debug_info("%s got name: %s", __func__, type_spec_node->identifier.identifier->text);
    return type_spec_node->identifier.identifier->text;
}

TypeDescriptor const *
find_typedef_type_descriptor(ir_generator_ctx_t * ctx, char const * name)
{
    TypeDescriptor const * result = scope_find_typedef_type_descriptor(ctx->current_scope, name);
    return result;
}

TypeDescriptor const *
find_type_descriptor_by_tag(ir_generator_ctx_t * ctx, char const * name)
{
    type_info_t * info = scope_find_tagged_struct(ctx->current_scope, name);
    if (info == NULL)
    {
        info = scope_find_tagged_union(ctx->current_scope, name);
    }
    if (info == NULL)
    {
        info = scope_find_tagged_enum(ctx->current_scope, name);
    }
    return info ? info->type_desc : NULL;
}

char *
generate_anon_name(ir_generator_ctx_t * ctx, char const * prefix)
{
    char * name = malloc(64);
    sprintf(name, ".anon.%s.%d", prefix, ctx->anon_counter++);
    return name;
}

int
evaluate_enum_value_assignment_expression(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * value_node, int current_value
)
{
    if (value_node == NULL)
    {
        return current_value;
    }

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
            if (find_symbol(ctx, value_node->text, &symbol))
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

            debug_info("%s: registering: %s value: %d", __func__, enum_name, current_value);

            LLVMValueRef const_val = LLVMConstInt(enum_type->llvm_type, current_value, true);
            LLVMValueRef global = LLVMAddGlobal(ctx->module, enum_type->llvm_type, enum_name);
            LLVMSetInitializer(global, const_val);
            LLVMSetGlobalConstant(global, true);
            LLVMSetLinkage(global, LLVMInternalLinkage);

            TypedValue val = create_typed_value(global, enum_type, false);
            add_symbol(ctx, enum_name, val);

            current_value++;
        }
    }

    return true;
}

c_grammar_node_t const *
search_parameters_list_in_declarator(c_grammar_node_t const * declarator_node)
{
    if (declarator_node == NULL)
    {
        return NULL;
    }
    c_grammar_node_t const * suffix_list = declarator_node->declarator.declarator_suffix_list;
    if (suffix_list->list.count == 0)
    {
        return NULL;
    }
    c_grammar_node_t const * suffix = suffix_list->list.children[0];
    if (suffix->type != AST_NODE_DECLARATOR_SUFFIX || suffix->list.count == 0)
    {
        return NULL;
    }
    c_grammar_node_t const * parameters_list = suffix->list.children[0];
    if (parameters_list->type != AST_NODE_PARAMETER_LIST)
    {
        return NULL;
    }

    return parameters_list;
}

c_grammar_node_t const *
extract_parameter_list(c_grammar_node_t const * suffix)
{
    if (suffix == NULL)
    {
        return NULL;
    }
    if (suffix->type == AST_NODE_PARAMETER_LIST)
    {
        return suffix;
    }
    if (suffix->list.count > 0)
    {
        c_grammar_node_t const * child = suffix->list.children[0];
        if (child->type == AST_NODE_PARAMETER_LIST)
        {
            return child;
        }
    }
    return NULL;
}

bool
is_function_suffix(c_grammar_node_t const * suffix)
{
    c_grammar_node_t const * param_list = extract_parameter_list(suffix);
    return param_list != NULL;
}

char const *
search_for_identifier(c_grammar_node_t const * node)
{
    if (node == NULL)
    {
        return NULL;
    }

    if (node->type == AST_NODE_DECLARATOR)
    {
        char const * var_name = NULL;
        c_grammar_node_t const * direct_decl_node = node->declarator.direct_declarator;

        // For regular variables: DirectDeclarator -> Identifier
        // For function pointers: DirectDeclarator -> FunctionPointerDeclarator -> {Pointer, Identifier,
        // DeclaratorSuffix*}
        if (direct_decl_node && direct_decl_node->list.count > 0)
        {
            c_grammar_node_t * first_child = direct_decl_node->list.children[0];
            if (first_child->type == AST_NODE_IDENTIFIER)
            {
                var_name = first_child->text;
            }
            else if (first_child->type == AST_NODE_DECLARATOR)
            {
                // Nested declarator (e.g., for function pointers like *name)
                // Find the DirectDeclarator inside and get the Identifier
                c_grammar_node_t const * nested_direct = first_child->declarator.direct_declarator;
                if (nested_direct != NULL)
                {
                    char const * id = search_for_identifier(nested_direct);
                    if (id != NULL)
                    {
                        var_name = id;
                    }
                }
            }
            else if (first_child->type == AST_NODE_FUNCTION_POINTER_DECLARATOR)
            {
                // FunctionPointerDeclarator: contains Pointer, Identifier, DeclaratorSuffix*
                var_name = first_child->function_pointer_declarator.identifier->text;
            }
        }

        return var_name;
    }

    for (size_t i = 0; i < node->list.count; i++)
    {
        c_grammar_node_t * child = node->list.children[i];

        if (child->type == AST_NODE_IDENTIFIER && child->text != NULL)
        {
            return child->text;
        }
    }

    return NULL;
}

unsigned
get_fp_width(LLVMTypeRef type)
{
    LLVMTypeKind kind = LLVMGetTypeKind(type);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

    switch (kind)
    {
    case LLVMHalfTypeKind:
        return 16;
    case LLVMFloatTypeKind:
        return 32;
    case LLVMDoubleTypeKind:
        return 64;
    case LLVMX86_FP80TypeKind:
        return 80;
    case LLVMFP128TypeKind:
        return 128;
    default:
        return 0; // Not a floating-point type
    }

#pragma GCC diagnostic pop
}

uint64_t
get_type_alignment(ir_generator_ctx_t * ctx, LLVMTypeRef type)
{
    if (type == NULL || LLVMGetTypeKind(type) == LLVMVoidTypeKind)
    {
        return 1;
    }

    // 1. Get the Data Layout from the module
    // This contains the rules for your specific target (x86, ARM, etc.)
    LLVMTargetDataRef data_layout = LLVMGetModuleDataLayout(ctx->module);

    // 2. Query the Preferred (or ABI) alignment directly as a number
    // LLVMABIAlignmentOfType returns the actual unsigned int you want.
    uint64_t alignment = LLVMABIAlignmentOfType(data_layout, type);

    debug_info("Type kind %u has alignment: %u", LLVMGetTypeKind(type), alignment);

    return alignment;
}

// Helper function to get size in bytes for a type
uint64_t
get_type_size(ir_generator_ctx_t * ctx, TypeDescriptor const * type)
{
    uint64_t size_in_bytes = get_type_size_desc(ctx->data_layout, type);
    debug_info("type size: %llu", size_in_bytes);
    return size_in_bytes;
}

bool
function_signatures_match(LLVMTypeRef type1, LLVMTypeRef type2)
{
    return type1 == type2;
}
