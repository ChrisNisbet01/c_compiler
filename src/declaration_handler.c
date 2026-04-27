#include "declaration_handler.h"

#include "ast_node_name.h"
#include "debug.h"
#include "type_descriptors.h"
#include "type_utils.h"

#include <stdio.h>
#include <string.h>

static TypeDescriptor const * resolve_function_pointer_type(
    ir_generator_ctx_t * ctx, TypeDescriptor const * return_type, c_grammar_node_t const * suffix
);

static TypeDescriptor const *
resolve_array_suffix(ir_generator_ctx_t * ctx, TypeDescriptor const * element_type, c_grammar_node_t const * suffix)
{
    debug_info("%s: resolving array type descriptor for element type %p", __func__, (void *)element_type);
    size_t size = 0;
    for (size_t i = 0; i < suffix->list.count; i++)
    {
        c_grammar_node_t const * child = suffix->list.children[i];
        if (child->type == AST_NODE_INTEGER_LITERAL)
        {
            size = (size_t)child->integer_lit.integer_literal.value;
        }
    }
    return get_or_create_array_type(ctx->type_descriptors, element_type, size);
}

TypeDescriptor const *
resolve_type_descriptor(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * specifiers, c_grammar_node_t const * declarator
)
{
    debug_info(
        "%s: resolving type descriptor for specifiers %p and declarator %p",
        __func__,
        (void *)specifiers,
        (void *)declarator
    );
    if (specifiers == NULL)
    {
        debug_info("%s: no specifiers provided, cannot resolve type descriptor", __func__);
        return NULL;
    }

    c_grammar_node_t const * type_spec_list = NULL;
    c_grammar_node_t const * type_qual_list = NULL;

    if (specifiers->type == AST_NODE_NAMED_DECL_SPECIFIERS)
    {
        type_spec_list = specifiers->decl_specifiers.type_specifiers;
        type_qual_list = specifiers->decl_specifiers.type_qualifiers;
    }
    TypeSpecifierValidationResult validation_result = validate_type_specifiers(type_spec_list);
    if (!validation_result.is_valid)
    {
        ir_gen_error(&ctx->errors, "Neither struct/union/enum/typedef nor native type specified in declaration");
        return NULL;
    }
    else
    {
        debug_info(
            "%s: type specifiers are valid - is_native_type %d, is_struct_or_union_or_enum %d",
            __func__,
            validation_result.is_native_type,
            validation_result.is_struct_or_union_or_enum
        );
    }
    TypeDescriptor const * current = NULL;

    if (type_spec_list != NULL && type_spec_list->list.count > 0)
    {
        c_grammar_node_t const * type_spec_node = type_spec_list->list.children[0];

        if (type_spec_node->list.count > 0 && validation_result.is_struct_or_union_or_enum)
        {
            c_grammar_node_t const * inner = type_spec_node->list.children[0];

            if (inner->type == AST_NODE_TYPEDEF_SPECIFIER)
            {
                char const * name = extract_typedef_name(inner);
                if (name != NULL)
                {
                    current = find_typedef_type_descriptor(ctx, name);
                }
            }
            else if (inner->type == AST_NODE_STRUCT_DEFINITION || inner->type == AST_NODE_UNION_DEFINITION)
            {
                current = register_struct_definition(ctx, inner)->type_desc;
            }
            else if (inner->type == AST_NODE_STRUCT_TYPE_REF || inner->type == AST_NODE_UNION_TYPE_REF)
            {
                char const * tag = extract_struct_or_union_or_enum_tag(inner);
                if (tag != NULL)
                {
                    current = find_type_descriptor_by_tag(ctx, tag);
                }
            }
        }
        else if (validation_result.is_native_type)
        {
            TypeQualifier quals = {0};
            if (type_qual_list != NULL)
            {
                quals = build_type_qualifiers(type_qual_list);
            }
            TypeSpecifier specs = build_type_specifiers(type_spec_list);
            if (type_specifier_is_valid(specs))
            {
                current = get_or_create_builtin_type(ctx->type_descriptors, specs, quals);
            }
            else
            {
                ir_gen_error(&ctx->errors, "Invalid combination of type specifiers in declaration");
                type_specifier_dump(specs, DEBUG_LEVEL_ERROR);
                return NULL;
            }
        }
        else
        {
            ir_gen_error(&ctx->errors, "Unsupported type specifier combination in declaration");
            return NULL;
        }
    }

    if (current == NULL)
    {
        return NULL;
    }

    if (declarator == NULL)
    {
        return current;
    }

    c_grammar_node_t const * pointer_list = declarator->declarator.pointer_list;
    for (size_t i = pointer_list->list.count; i > 0; i--)
    {
        c_grammar_node_t const * pointer_node = pointer_list->list.children[i - 1];
        TypeQualifier ptr_quals = {0};
        if (pointer_node->list.count > 0)
        {
            ptr_quals = build_type_qualifiers(pointer_node->list.children[0]);
        }
        current = get_or_create_pointer_type(ctx->type_descriptors, current, ptr_quals);
    }

    c_grammar_node_t const * suffix_list = declarator->declarator.declarator_suffix_list;
    c_grammar_node_t const * direct_declarator = declarator->declarator.direct_declarator;
    c_grammar_node_t const * fn_ptr_declarator
        = direct_declarator->list.count > 0 ? direct_declarator->list.children[0] : NULL;

    bool is_function = suffix_list->list.count > 0 && is_function_suffix(suffix_list->list.children[0]);

    if (is_function)
    {
        for (size_t i = 0; i < suffix_list->list.count; i++)
        {
            c_grammar_node_t const * suffix = suffix_list->list.children[i];

            current = resolve_function_pointer_type(ctx, current, suffix);
        }
    }
    else
    {
        for (size_t i = suffix_list->list.count; i > 0; i--)
        {
            c_grammar_node_t const * suffix = suffix_list->list.children[i - 1];

            current = resolve_array_suffix(ctx, current, suffix);
        }
    }

    return current;
}

static TypeDescriptor const *
resolve_function_pointer_type(
    ir_generator_ctx_t * ctx, TypeDescriptor const * return_type, c_grammar_node_t const * suffix
)
{
    c_grammar_node_t const * param_list = extract_parameter_list(suffix);
    if (param_list == NULL)
    {
        param_list = suffix;
    }

    TypeDescriptor const * params[16] = {NULL};
    size_t param_count = 0;
    bool is_variadic = false;

    for (size_t i = 0; i < param_list->list.count && param_count < 16; i++)
    {
        c_grammar_node_t const * param = param_list->list.children[i];
        if (param->type == AST_NODE_ELLIPSIS)
        {
            is_variadic = true;
            break;
        }
        else if (param->type == AST_NODE_NAMED_DECL_SPECIFIERS)
        {
            params[param_count++] = resolve_type_descriptor(ctx, param, NULL);
        }
        else if (param->type == AST_NODE_DECLARATOR)
        {
            c_grammar_node_t const * decl_specs = param;
            params[param_count++] = resolve_type_descriptor(ctx, decl_specs, NULL);
        }
    }

    return get_or_create_function_type(ctx->type_descriptors, return_type, params, param_count, is_variadic);
}
