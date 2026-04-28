#include "declaration_handler.h"

#include "ast_node_name.h"
#include "debug.h"
#include "type_descriptors.h"
#include "type_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    c_grammar_node_t const * decl_specifiers;
    c_grammar_node_t const * declarator;
} param_nodes;

typedef struct
{
    size_t count;
    bool is_variadic;
    param_nodes * nodes;
    char const ** names;
    TypeDescriptor const ** types;
} parameter_definitions_t;

static void
parameter_definitions_cleanup(parameter_definitions_t * params)
{
    free(params->names);
    free(params->types);
}

static bool
parameter_definitions_init(parameter_definitions_t * params, c_grammar_node_t const * params_list_node)
{
    if (params_list_node == NULL || params_list_node->type != AST_NODE_PARAMETER_LIST)
    {
        debug_error("Invalid parameter list node");
        return false;
    }

    size_t params_list_count = params_list_node->list.count;
    if (params_list_count > 1 && params_list_node->list.children[params_list_count - 1]->type == AST_NODE_ELLIPSIS)
    {
        params_list_count--; /* Exclude ellipsis from count */
    }
    /* There are either 2 or 3 nodes per param - allocate enough space assuming 2 nodes per param. */
    params->names = calloc(params_list_count / 2, sizeof(*params->names));
    params->types = calloc(params_list_count / 2, sizeof(*params->types));
    params->nodes = calloc(params_list_count / 2, sizeof(*params->nodes));
    if (params->names == NULL || params->types == NULL || params->nodes == NULL)
    {
        debug_error("failed to init param definitions");
        parameter_definitions_cleanup(params);
        return false;
    }

    size_t count = 0;
    for (size_t i = 0; i < params_list_count; i++)
    {
        c_grammar_node_t const * child = params_list_node->list.children[i];
        if (child->type == AST_NODE_NAMED_DECL_SPECIFIERS)
        {
            debug_info("got decl specifiers for param %zu", count);
            params->nodes[count].decl_specifiers = child;
            if (i < params_list_count - 1 && params_list_node->list.children[i + 1]->type == AST_NODE_DECLARATOR)
            {
                debug_info("got declarator for param %zu", count);
                params->nodes[count].declarator = params_list_node->list.children[i + 1];
            }
            count++;
        }
    }

    params->count = count;

    return true;
}

static parameter_definitions_t
extract_function_parameters(ir_generator_ctx_t * ctx, c_grammar_node_t const * params_list)
{
    debug_info("%s", __func__);
    parameter_definitions_t params = {0};

    if (params_list != NULL && params_list->list.count > 0)
    {
        // Each parameter has [KwExtension, TypeSpecifier, Declarator?]
        // Note that the Declarator may NOT be present.
        if (!parameter_definitions_init(&params, params_list))
        {
            ir_gen_error(&ctx->errors, "Memory error");
            return params;
        }
        debug_info("%s: extracting %zu parameters", __func__, params.count);
        for (size_t i = 0; i < params.count; ++i)
        {
            c_grammar_node_t const * p_spec = params.nodes[i].decl_specifiers;
            c_grammar_node_t const * p_decl = params.nodes[i].declarator;
            debug_info(
                "%s: processing parameter spec %s decl %s",
                __func__,
                get_node_type_name_from_node(p_spec),
                get_node_type_name_from_node(p_decl)
            );
            params.types[i] = resolve_type_descriptor(ctx, p_spec, p_decl);
            debug_info(
                "%s: resolved parameter %u type descriptor with LLVM type kind %d",
                __func__,
                i,
                LLVMGetTypeKind(params.types[i]->llvm_type)
            );
            if (LLVMGetTypeKind(params.types[i]->llvm_type) == LLVMFunctionTypeKind)
            {
                debug_info("%s: parameter %zu is a function type - converting to pointer type", __func__, i);
                params.types[i]
                    = get_or_create_pointer_type(ctx->type_descriptors, params.types[i], (TypeQualifier){0});
            }

            if (p_decl != NULL)
            {
                c_grammar_node_t const * p_direct = p_decl->declarator.direct_declarator;
                debug_info("%s: direct declarator node %s", __func__, get_node_type_name_from_node(p_direct));
                if (p_direct != NULL && p_direct->list.count > 0)
                {
                    c_grammar_node_t const * first_child = p_direct->list.children[0];
                    if (first_child->type == AST_NODE_IDENTIFIER)
                    {
                        params.names[i] = first_child->text;
                    }
                    else if (first_child->type == AST_NODE_DECLARATOR)
                    {
                        // Nested declarator (e.g., for function pointers like *name)
                        // Find the DirectDeclarator inside and get the Identifier
                        c_grammar_node_t const * nested_direct = first_child->declarator.direct_declarator;
                        if (nested_direct && nested_direct->list.count > 0
                            && nested_direct->list.children[0]->type == AST_NODE_IDENTIFIER)
                        {
                            params.names[i] = nested_direct->list.children[0]->text;
                        }
                    }
                    else if (first_child->type == AST_NODE_FUNCTION_POINTER_DECLARATOR)
                    {
                        // FunctionPointerDeclarator: contains Pointer, Identifier, DeclaratorSuffix*
                        char const * id = first_child->function_pointer_declarator.identifier->text;
                        if (id != NULL)
                        {
                            params.names[i] = id;
                        }
                    }
                }
            }
        }
    }

    if (params.count == 1 && params.types[0]->specifiers.is_void)
    {
        debug_info("%s: single parameter of type void - treating as no parameters", __func__);
        params.count = 0;
    }

    return params;
}

static TypeDescriptor const *
resolve_function_pointer_type(
    ir_generator_ctx_t * ctx, TypeDescriptor const * return_type, c_grammar_node_t const * param_list
)
{
    debug_info(
        "%s: resolving function pointer type for return type %d and param list %p",
        __func__,
        LLVMGetTypeKind(return_type->llvm_type),
        (void *)param_list
    );
    parameter_definitions_t params = extract_function_parameters(ctx, param_list);

    TypeDescriptor const * res = get_or_create_function_type(
        ctx->type_descriptors, return_type, params.types, params.names, params.count, params.is_variadic
    );
    parameter_definitions_cleanup(&params);
    return res;
}

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
            break;
        }
    }
    return get_or_create_array_type(ctx->type_descriptors, element_type, size);
}

static c_grammar_node_t const *
search_function_pointer_declarator(c_grammar_node_t const * node)
{
    if (node == NULL)
    {
        return NULL;
    }
    for (size_t i = 0; i < node->list.count; i++)
    {
        c_grammar_node_t const * child = node->list.children[i];
        if (child->type == AST_NODE_FUNCTION_POINTER_DECLARATOR)
        {
            return child;
        }
    }

    return NULL;
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

    if (specifiers->type == AST_NODE_STRUCT_SPECIFIER_QUALIFIER_LIST && specifiers->list.count == 1)
    {
        c_grammar_node_t const * child = specifiers->list.children[0];
        if (child->type == AST_NODE_TYPEDEF_SPECIFIER_QUALIFIER)
        {
            specifiers = child->typedef_specifier_qualifier.typedef_specifier;
            if (specifiers == NULL)
            {
                debug_info("%s: no specifiers provided, cannot resolve type descriptor", __func__);
                return NULL;
            }
        }
    }

    c_grammar_node_t const * type_spec_list = NULL;
    c_grammar_node_t const * type_qual_list = NULL;
    TypeSpecifierValidationResult validation_result = {0};

    if (specifiers->type == AST_NODE_NAMED_DECL_SPECIFIERS)
    {
        type_spec_list = specifiers->decl_specifiers.type_specifiers;
        validation_result = validate_type_specifiers(type_spec_list);

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
        type_qual_list = specifiers->decl_specifiers.type_qualifiers;
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
            else if (
                inner->type == AST_NODE_STRUCT_TYPE_REF || inner->type == AST_NODE_UNION_TYPE_REF
                || inner->type == AST_NODE_ENUM_TYPE_REF
            )
            {
                char const * tag = extract_struct_or_union_or_enum_tag(inner);
                debug_info("%s: looking up struct/union/enum tag '%s'", __func__, tag);
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

    c_grammar_node_t const * param_list = search_parameters_list_in_declarator(declarator);
    bool is_function = param_list != NULL;
    c_grammar_node_t const * suffix_list = declarator->declarator.declarator_suffix_list;

    if (is_function)
    {
        current = resolve_function_pointer_type(ctx, current, param_list);

        /* Now handle any function pointer array suffixes. */
        c_grammar_node_t const * direct_declarator = declarator->declarator.direct_declarator;
        c_grammar_node_t const * func_pointer_declaration = search_function_pointer_declarator(direct_declarator);
        if (func_pointer_declaration != NULL)
        {
            c_grammar_node_t const * fp_suffix_list
                = func_pointer_declaration->function_pointer_declarator.declarator_suffix_list;
            debug_info(
                "%s: found function pointer declarator, have %d array suffixes", __func__, fp_suffix_list->list.count
            );
            for (size_t i = fp_suffix_list->list.count; i > 0; i--)
            {
                c_grammar_node_t const * suffix = fp_suffix_list->list.children[i - 1];

                current = resolve_array_suffix(ctx, current, suffix);
            }
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
