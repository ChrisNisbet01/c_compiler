#include "declaration_handler.h"

#include "ast_node_name.h"
#include "ast_print.h"
#include "debug.h"
#include "generator_lists.h"
#include "type_descriptors.h"
#include "type_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
parameter_definitions_cleanup(parameter_definitions_t * params)
{
    free(params->names);
    free(params->types);
    params->count = 0;
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
        params->is_variadic = true;
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
            params->nodes[count].decl_specifiers = child;
            if (i < params_list_count - 1 && params_list_node->list.children[i + 1]->type == AST_NODE_DECLARATOR)
            {
                params->nodes[count].declarator = params_list_node->list.children[i + 1];
            }
            count++;
        }
    }

    params->count = count;

    return true;
}

parameter_definitions_t
extract_function_parameters(ir_generator_ctx_t * ctx, c_grammar_node_t const * params_list)
{
    parameter_definitions_t params = {0};

    if (params_list != NULL && params_list->list.count > 0)
    {
        // Each parameter has [KwExtension, TypeSpecifier, Declarator?]
        // Note that the Declarator may NOT be present.
        if (!parameter_definitions_init(&params, params_list))
        {
            ir_gen_error(&ctx->errors, params_list, "Memory error");
            return params;
        }

        for (size_t i = 0; i < params.count; i++)
        {
            c_grammar_node_t const * p_spec = params.nodes[i].decl_specifiers;
            c_grammar_node_t const * p_decl = params.nodes[i].declarator;

            params.types[i] = resolve_type_descriptor(ctx, p_spec, p_decl);
            if (LLVMGetTypeKind(params.types[i]->llvm_type) == LLVMFunctionTypeKind)
            {
                params.types[i]
                    = get_or_create_pointer_type(ctx->type_descriptors, params.types[i], (TypeQualifier){0});
            }

            if (params.types[i]->kind == NCC_TYPE_KIND_ARRAY)
            {
                params.types[i]
                    = get_or_create_pointer_type(ctx->type_descriptors, params.types[i]->pointee, (TypeQualifier){0});
            }

            if (p_decl != NULL)
            {
                c_grammar_node_t const * p_direct = p_decl->declarator.direct_declarator;

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
        params.count = 0;
    }

    return params;
}

static TypeDescriptor const *
resolve_function_pointer_type(
    ir_generator_ctx_t * ctx, TypeDescriptor const * return_type, c_grammar_node_t const * param_list
)
{
    parameter_definitions_t params = extract_function_parameters(ctx, param_list);
    TypeDescriptor const * res = get_or_create_function_type(
        ctx->type_descriptors, return_type, params.types, params.count, params.is_variadic
    );

    parameter_definitions_cleanup(&params);

    return res;
}

static c_grammar_node_t const *
search_for_node_type(c_grammar_node_t const * node, c_grammar_node_type_t type)
{
    if (node == NULL)
    {
        return NULL;
    }
    for (size_t i = 0; i < node->list.count; i++)
    {
        c_grammar_node_t const * child = node->list.children[i];
        if (child->type == type)
        {
            return child;
        }
    }

    return NULL;
}

static TypeDescriptor const *
resolve_array_suffix(ir_generator_ctx_t * ctx, TypeDescriptor const * element_type, c_grammar_node_t const * suffix)
{
    unsigned long long raw_val = 0;
    c_grammar_node_t const * child = NULL;

    for (size_t i = 0; i < suffix->list.count; i++)
    {
        c_grammar_node_t const * candidate = suffix->list.children[i];
        if (candidate->type != AST_NODE_TYPE_QUALIFIERS)
        {
            child = candidate;
            break;
        }
    }

    if (child != NULL)
    {
        TypedValue val = process_expression(ctx, child);

        if (val.value == NULL)
        {
            debug_error("%s: failed to process expression");
            return NULL;
        }

        if (!LLVMIsConstant(val.value))
        {
            debug_error("%s: expression result is not constant", __func__);
        }
        if (LLVMGetTypeKind(LLVMTypeOf(val.value)) != LLVMIntegerTypeKind)
        {
            debug_error("%s: expression result is not an integer", __func__);
            return NULL;
        }

        raw_val = LLVMConstIntGetSExtValue(val.value);
        if (raw_val == 0)
        {
            debug_error("failed to find array size");
            return NULL;
        }
    }

    TypeDescriptor const * array_type = get_or_create_array_type(ctx->type_descriptors, element_type, raw_val);

    return array_type;
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

static TypeDescriptor const *
apply_typedef_nested_pointers(
    ir_generator_ctx_t * ctx, TypeDescriptor const * current, c_grammar_node_t const * direct_declarator
)
{
    if (direct_declarator == NULL)
    {
        return current;
    }
    c_grammar_node_t const * nested = direct_declarator->typedef_direct_declarator.nested_typedef_declarator;
    if (nested == NULL)
    {
        return current;
    }

    /* Apply the nested TYPEDEF_DECLARATOR's pointer_list. */
    c_grammar_node_t const * nested_ptr_list = nested->typedef_declarator.pointer_list;
    if (nested_ptr_list != NULL)
    {
        for (size_t i = 0; i < nested_ptr_list->list.count; i++)
        {
            c_grammar_node_t const * pointer_node = nested_ptr_list->list.children[i];
            TypeQualifier ptr_quals = {0};
            if (pointer_node->list.count > 0)
            {
                ptr_quals = build_type_qualifiers(pointer_node->list.children[0]);
            }
            current = get_or_create_pointer_type(ctx->type_descriptors, current, ptr_quals);
        }
    }

    /* Recurse into further nesting inside the nested declarator. */
    if (nested->type == AST_NODE_TYPEDEF_DECLARATOR)
    {
        current = apply_typedef_nested_pointers(ctx, current, nested->typedef_declarator.direct_declarator);
    }

    return current;
}

TypeDescriptor const *
resolve_type_descriptor(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * decl_specifiers, c_grammar_node_t const * declarator
)
{
    if (decl_specifiers == NULL)
    {
        return NULL;
    }

    c_grammar_node_t const * type_spec_list = NULL;

    if (decl_specifiers->type == AST_NODE_TYPEDEF_SPECIFIER_QUALIFIER)
    {
        decl_specifiers = decl_specifiers->typedef_specifier_qualifier.typedef_specifier;
        if (decl_specifiers == NULL)
        {
            return NULL;
        }
    }

    if (decl_specifiers->type == AST_NODE_STRUCT_SPECIFIER_QUALIFIER_LIST)
    {
        c_grammar_node_t const * child = decl_specifiers->list.children[0];
        if (child->type == AST_NODE_TYPEDEF_SPECIFIER_QUALIFIER)
        {
            if (decl_specifiers->list.count == 1)
            {
                decl_specifiers = child->typedef_specifier_qualifier.typedef_specifier;
                if (decl_specifiers == NULL)
                {
                    return NULL;
                }
            }
        }
        else
        {
            type_spec_list = decl_specifiers;
        }
    }

    TypeSpecifierValidationResult validation_result = {0};
    scope_typedef_entry_t const * existing_typedef_info = NULL;
    char const * typedef_name = NULL;
    /* FIXME: only supported by native types right now. */
    TypeQualifier type_quals = build_type_qualifiers_from_parent(decl_specifiers);

    if (decl_specifiers->type == AST_NODE_TYPEDEF_SPECIFIER)
    {
        typedef_name = search_for_identifier(decl_specifiers);
    }
    else if (decl_specifiers->type == AST_NODE_NAMED_DECL_SPECIFIERS)
    {
        /* First check for a TYPEDEF_SPECIFIER, in which case there won't be any TYPE_SPECIFIERS. */
        c_grammar_node_t const * typedef_specifier_node = decl_specifiers->decl_specifiers.typedef_specifier;
        typedef_name = search_for_identifier(typedef_specifier_node);
    }

    if (typedef_name != NULL)
    {
        /* We should have a typedef of this name already registered. */
        existing_typedef_info = generator_lookup_typedef_entry_by_name(ctx, typedef_name);
    }

    if (type_spec_list == NULL && decl_specifiers->type != AST_NODE_TYPEDEF_SPECIFIER)
    {
        type_spec_list = decl_specifiers->decl_specifiers.type_specifiers;
    }

    if (type_spec_list != NULL)
    {
        validation_result = validate_type_specifiers(type_spec_list);

        if (!validation_result.is_valid)
        {
            ir_gen_error(
                &ctx->errors,
                type_spec_list,
                "Neither struct/union/enum/typedef nor native type nor typeof specified in declaration"
            );
            return NULL;
        }
    }

    TypeDescriptor const * current = existing_typedef_info != NULL ? existing_typedef_info->type_desc : NULL;

    if (type_spec_list != NULL && type_spec_list->list.count > 0)
    {
        c_grammar_node_t const * type_spec_node = type_spec_list->list.children[type_spec_list->list.count - 1];

        if (type_spec_node->list.count > 0 && validation_result.is_struct_or_union_or_enum)
        {
            c_grammar_node_t const * inner = type_spec_node->list.children[0];

            if (inner->type == AST_NODE_TYPEDEF_SPECIFIER)
            {
                if (current == NULL)
                {
                    char const * name = extract_typedef_name(inner);
                    if (name != NULL)
                    {
                        current = generator_find_typedef_type_descriptor(ctx, name);
                    }
                }
            }
            else if (inner->type == AST_NODE_STRUCT_DEFINITION || inner->type == AST_NODE_UNION_DEFINITION)
            {
                type_info_t const * type_info = register_struct_definition(ctx, inner);
                if (type_info == NULL)
                {
                    debug_error("%s: failed to register struct definition", __func__);
                    ir_gen_error(&ctx->errors, inner, "Failed to register struct definition");
                    return NULL;
                }
                current = type_info->type_desc;
            }
            else if (inner->type == AST_NODE_ENUM_DEFINITION)
            {
                if (current == NULL)
                {
                    type_info_t const * type_info = register_enum_definition(ctx, inner);
                    if (type_info == NULL)
                    {
                        debug_error("%s: failed to register enum definition", __func__);
                        ir_gen_error(&ctx->errors, inner, "Failed to register enum definition");
                        return NULL;
                    }
                    current = type_info->type_desc;
                }
            }
            else if (
                inner->type == AST_NODE_STRUCT_TYPE_REF || inner->type == AST_NODE_UNION_TYPE_REF
                || inner->type == AST_NODE_ENUM_TYPE_REF
            )
            {
                if (current == NULL)
                {
                    type_kind_t kind = TYPE_KIND_COUNT__;
                    char const * tag = extract_struct_or_union_or_enum_tag(inner, &kind);

                    if (tag == NULL)
                    {
                        ir_gen_error(&ctx->errors, type_spec_node, "Missing struct/union/enum tag");
                        return NULL;
                    }
                    current = generator_find_type_descriptor_by_tag_and_kind(ctx, tag, kind);
                    if (current == NULL)
                    {
                        if (inner->type == AST_NODE_ENUM_TYPE_REF)
                        {
                            return type_descriptor_get_enum_type(ctx->type_descriptors);
                        }
                        type_info_t const * opaque_info = register_incomplete_struct_or_union_definition(
                            ctx,
                            tag,
                            type_quals,
                            inner->type == AST_NODE_UNION_TYPE_REF ? TYPE_KIND_UNION : TYPE_KIND_STRUCT
                        );
                        if (opaque_info == NULL)
                        {
                            return NULL;
                        }
                        current = opaque_info->type_desc;
                    }
                }
            }
        }
        else if (validation_result.is_native_type && current == NULL)
        {
            TypeSpecifier specs = build_type_specifiers(type_spec_list);
            if (type_specifier_is_valid(specs))
            {
                current = get_or_create_builtin_type(ctx->type_descriptors, specs, type_quals);
            }
            else
            {
                ir_gen_error(&ctx->errors, type_spec_list, "Invalid combination of type specifiers in declaration");

                return NULL;
            }
        }
        else if (validation_result.is_typeof && current == NULL)
        {
            c_grammar_node_t const * typeof_node = type_spec_node->list.children[0];
            c_grammar_node_t const * typeof_specifier_node = typeof_node->typeof_specifier.specifier;

            if (typeof_specifier_node->type == AST_NODE_TYPE_NAME)
            {
                current = get_type_descriptor_from_type_name(ctx, typeof_specifier_node);
                if (current == NULL)
                {
                    debug_error("typeof specifier processing failed for typeof type name");
                    ir_gen_error(&ctx->errors, typeof_node, "typeof specifier processing failed for typeof type name");
                    return NULL;
                }
            }
            else
            {
                TypedValue operand_res = process_expression(ctx, typeof_specifier_node);
                current = operand_res.type_info;
                if (current == NULL)
                {
                    debug_error("typeof specifier processing failed for typeof expression");
                    ir_gen_error(&ctx->errors, typeof_node, "typeof specifier processing failed for typeof expression");
                    return NULL;
                }
            }
        }
        else if (current == NULL)
        {
            ir_gen_error(&ctx->errors, type_spec_node, "Unsupported type specifier combination in declaration");
            return NULL;
        }
    }

    if (current == NULL)
    {
        return NULL;
    }

    current = get_or_create_qualified_type(ctx->type_descriptors, current, type_quals);

    if (declarator == NULL)
    {
        return current;
    }

    c_grammar_node_t const * pointer_list = NULL;
    c_grammar_node_t const * param_list = NULL;
    c_grammar_node_t const * suffix_list = NULL;
    c_grammar_node_t const * direct_declarator = NULL;

    if (declarator->type == AST_NODE_TYPEDEF_DECLARATOR)
    {
        pointer_list = declarator->typedef_declarator.pointer_list;
        suffix_list = declarator->typedef_declarator.declarator_suffix_list;
        direct_declarator = declarator->typedef_declarator.direct_declarator;
        if (suffix_list != NULL && suffix_list->list.count > 0)
        {
            c_grammar_node_t const * suffix = suffix_list->list.children[0];
            if (suffix->type == AST_NODE_DECLARATOR_SUFFIX && suffix->list.count > 0)
            {
                c_grammar_node_t const * parameters_list = suffix->list.children[0];
                if (parameters_list->type == AST_NODE_PARAMETER_LIST)
                {
                    param_list = parameters_list;
                }
            }
        }
    }
    else if (declarator->type == AST_NODE_DECLARATOR)
    {
        pointer_list = declarator->declarator.pointer_list;
        param_list = search_parameters_list_in_declarator(declarator);
        suffix_list = declarator->declarator.declarator_suffix_list;
        direct_declarator = declarator->declarator.direct_declarator;
    }
    else if (declarator->type == AST_NODE_ABSTRACT_DECLARATOR)
    {
        pointer_list = search_for_node_type(declarator, AST_NODE_POINTER_LIST);
        suffix_list = search_for_node_type(declarator, AST_NODE_DECLARATOR_SUFFIX_LIST);
    }
    else
    {
        /* What? */
        debug_error("%s Unsupported declarator type: %s", __func__, get_node_type_name_from_node(declarator));
        return NULL;
    }

    bool is_function = param_list != NULL;

    /*
     * For TYPEDEF_DECLARATOR with a function parameter list, the pointer_list
     * wraps the function type (not the return type). The pointer list must
     * be applied after the function type is created, so defer it here.
     */
    bool defer_pointers = (is_function && declarator->type == AST_NODE_TYPEDEF_DECLARATOR);

    if (!defer_pointers)
    {
        if (pointer_list != NULL)
        {
            for (size_t i = pointer_list->list.count; i > 0; i--)
            {
                c_grammar_node_t const * pointer_node = pointer_list->list.children[i - 1];
                TypeQualifier ptr_quals = {0};

                ptr_quals = build_type_qualifiers(pointer_node->list.children[0]);
                current = get_or_create_pointer_type(ctx->type_descriptors, current, ptr_quals);
            }
        }
    }

    if (is_function)
    {
        current = resolve_function_pointer_type(ctx, current, param_list);

        /*
         * Apply deferred pointers from TYPEDEF_DECLARATOR.
         * These come from the outer pointer_list and also from nested
         * TYPEDEF_DECLARATORs inside the direct_declarator (from parentheses
         * around the declarator, e.g. `typedef void (*func_t)(void)`).
         */
        if (defer_pointers)
        {
            if (pointer_list != NULL)
            {
                for (size_t i = pointer_list->list.count; i > 0; i--)
                {
                    c_grammar_node_t const * pointer_node = pointer_list->list.children[i - 1];
                    TypeQualifier ptr_quals = {0};

                    ptr_quals = build_type_qualifiers(pointer_node->list.children[0]);
                    current = get_or_create_pointer_type(ctx->type_descriptors, current, ptr_quals);
                }
            }
            current = apply_typedef_nested_pointers(ctx, current, direct_declarator);
        }

        /* Now handle any function pointer array suffixes. */
        c_grammar_node_t const * func_pointer_declarator = search_function_pointer_declarator(direct_declarator);
        if (func_pointer_declarator != NULL)
        {
            c_grammar_node_t const * id_node = func_pointer_declarator->function_pointer_declarator.identifier;
            char const * id = id_node->text;

            if (id == NULL)
            {
                debug_error("function pointer declarator has no identifier");
                return NULL;
            }

            c_grammar_node_t const * fp_suffix_list
                = func_pointer_declarator->function_pointer_declarator.declarator_suffix_list;

            if (current != NULL)
            {
                current = get_or_create_pointer_type(ctx->type_descriptors, current, (TypeQualifier){0});
            }

            for (size_t i = fp_suffix_list->list.count; i > 0; i--)
            {
                c_grammar_node_t const * suffix = fp_suffix_list->list.children[i - 1];

                current = resolve_array_suffix(ctx, current, suffix);
            }
            if (current == NULL)
            {
                debug_error("failed to resolve function pointer type");
                return NULL;
            }
        }
    }
    else if (suffix_list != NULL)
    {
        for (size_t i = suffix_list->list.count; i > 0; i--)
        {
            c_grammar_node_t const * suffix = suffix_list->list.children[i - 1];

            current = resolve_array_suffix(ctx, current, suffix);
            if (current == NULL)
            {
                debug_error("failed to resolve array suffix");
                print_ast_with_label(suffix_list, "suffix list");
                return NULL;
            }
        }
    }

    return current;
}
