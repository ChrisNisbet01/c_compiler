#include "declaration_handler.h"

#include "ast_node_name.h"
#include "ast_print.h"
#include "debug.h"
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
        debug_info("is variadic function");
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

parameter_definitions_t
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
            ir_gen_error(&ctx->errors, params_list, "Memory error");
            return params;
        }
        debug_info("%s: extracting %zu parameters", __func__, params.count);
        for (size_t i = 0; i < params.count; i++)
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
                    print_ast_with_label(first_child, "direct decl child");
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
        ctx->type_descriptors, return_type, params.types, params.count, params.is_variadic
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

TypeDescriptor const *
resolve_type_descriptor(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * decl_specifiers, c_grammar_node_t const * declarator
)
{
    debug_info(
        "%s: resolving type descriptor for decl_specifiers %s and declarator %s",
        __func__,
        decl_specifiers != NULL ? get_node_type_name_from_node(decl_specifiers) : "NULL",
        declarator != NULL ? get_node_type_name_from_node(declarator) : "NULL"
    );
    if (decl_specifiers == NULL)
    {
        debug_info("%s: no decl_specifiers provided, cannot resolve type descriptor", __func__);
        return NULL;
    }

    c_grammar_node_t const * type_spec_list = NULL;

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
                    debug_info("%s: no decl_specifiers provided, cannot resolve type descriptor", __func__);
                    return NULL;
                }
            }
        }
        else
        {
            type_spec_list = decl_specifiers;
            debug_info("set type spec list to %s", get_node_type_name_from_node(type_spec_list));
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
        debug_info("%s: Processing typedef '%s'", __func__, typedef_name);
        /* We should have a typedef of this name already registered. */
        existing_typedef_info = scope_lookup_typedef_entry_by_name(ctx->current_scope, typedef_name);
        if (existing_typedef_info != NULL)
        {
            debug_info("%s: Found typedef descriptor for '%s'", __func__, typedef_name);
        }
        else
        {
            debug_info("%s: No typedef descriptor found for '%s'", __func__, typedef_name);
        }
    }
    else
    {
        debug_info("%s: No typedef name found", __func__);
    }

    if (type_spec_list == NULL && existing_typedef_info == NULL)
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
                "Neither struct/union/enum/typedef nor native type specified in declaration"
            );
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
    }

    TypeDescriptor const * current = existing_typedef_info != NULL ? existing_typedef_info->type_desc : NULL;

    debug_info("current type descriptor: %p", (void *)current);

    if (current == NULL && type_spec_list != NULL && type_spec_list->list.count > 0)
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
                if (current == NULL)
                {
                    if (inner->type == AST_NODE_ENUM_TYPE_REF)
                    {
                        /* What to do? */
                        return NULL;
                    }
                    type_info_t const * opaque_info
                        = register_opaque_struct_or_union_definition(ctx, tag, inner->type == AST_NODE_UNION_TYPE_REF);
                    current = opaque_info->type_desc;
                }
            }
        }
        else if (validation_result.is_native_type)
        {
            TypeSpecifier specs = build_type_specifiers(type_spec_list);
            if (type_specifier_is_valid(specs))
            {
                current = get_or_create_builtin_type(ctx->type_descriptors, specs, type_quals);
            }
            else
            {
                ir_gen_error(&ctx->errors, type_spec_list, "Invalid combination of type specifiers in declaration");
                type_specifier_dump(specs, DEBUG_LEVEL_ERROR);
                return NULL;
            }
        }
        else
        {
            ir_gen_error(&ctx->errors, type_spec_node, "Unsupported type specifier combination in declaration");
            return NULL;
        }
    }

    if (current == NULL)
    {
        debug_info("no type descriptor found, returning NULL");
        return NULL;
    }

    if (declarator == NULL)
    {
        debug_info("%s: no declarator provided, returning type descriptor", __func__);
        return current;
    }
    debug_info("declarator node: %s", get_node_type_name_from_node(declarator));

    c_grammar_node_t const * pointer_list = NULL;
    c_grammar_node_t const * param_list = NULL;
    c_grammar_node_t const * suffix_list = NULL;
    c_grammar_node_t const * direct_declarator = NULL;

    if (declarator->type == AST_NODE_TYPEDEF_DECLARATOR)
    {
        pointer_list = declarator->typedef_declarator.pointer_list;
        suffix_list = declarator->typedef_declarator.declarator_suffix_list;
        direct_declarator = declarator->typedef_declarator.direct_declarator;
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

    debug_info("1");
    if (pointer_list != NULL)
    {
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
    }
    debug_info("2");

    bool is_function = param_list != NULL;

    if (is_function)
    {
        debug_info("3");
        current = resolve_function_pointer_type(ctx, current, param_list);
        dump_type_descriptor("function pointer", current, DEBUG_LEVEL_INFO);

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
            debug_info("function pointer ID: %s", id);
            c_grammar_node_t const * fp_suffix_list
                = func_pointer_declarator->function_pointer_declarator.declarator_suffix_list;
            debug_info(
                "%s: found function pointer declarator, have %d array suffixes", __func__, fp_suffix_list->list.count
            );

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
            dump_type_descriptor(id, current, DEBUG_LEVEL_INFO);
        }
    }
    else if (suffix_list != NULL)
    {
        debug_info("4");
        for (size_t i = suffix_list->list.count; i > 0; i--)
        {
            c_grammar_node_t const * suffix = suffix_list->list.children[i - 1];

            current = resolve_array_suffix(ctx, current, suffix);
        }
    }
    debug_info("%s out: current %p", __func__, current);
    return current;
}

struct_or_union_members_st
extract_struct_or_union_members_type_descriptor(ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child)
{
    debug_info("%s:", __func__);
    struct_or_union_members_st object_members = {0};

    if (type_child == NULL
        || (type_child->type != AST_NODE_STRUCT_DEFINITION && type_child->type != AST_NODE_UNION_DEFINITION))
    {
        debug_warning("Need struct or union definition, but got: %s", get_node_type_name_from_node(type_child));
        return object_members;
    }

    c_grammar_node_t const * members_node = type_child->struct_definition.declaration_list;

    if (members_node == NULL || members_node->list.count == 0)
    {
        debug_warning("no members declaration");
        return object_members;
    }

    size_t max_num_members = members_node->list.count;
    struct_field_t * members = calloc(max_num_members, sizeof(*members));
    if (members == NULL)
    {
        return object_members;
    }

    unsigned num_members = 0;

    for (size_t i = 0; i < members_node->list.count; i++)
    {
        debug_info("check member %u and num_members %u", i, num_members);
        c_grammar_node_t * struct_decl = members_node->list.children[i];
        if (struct_decl == NULL || struct_decl->type != AST_NODE_STRUCT_DECLARATION)
        {
            debug_info("struct declaration missing - skipping");
            continue;
        }

        c_grammar_node_t const * specifier_qualifier_list = struct_decl->struct_declaration.specifier_qualifier_list;
        c_grammar_node_t const * declarator_list = struct_decl->struct_declaration.declarator_list;

        if (specifier_qualifier_list == NULL || specifier_qualifier_list->list.count == 0)
        {
            debug_info(
                "spec qualifiers list missing or empty (%s) - skipping",
                get_node_type_name_from_node(specifier_qualifier_list)
            );
            continue;
        }
        debug_info("%s: spec_qual_list_type is %s", __func__, get_node_type_name_from_node(specifier_qualifier_list));

        c_grammar_node_t const * type_spec = NULL;
        type_spec = specifier_qualifier_list;

        if (declarator_list == NULL || declarator_list->list.count == 0)
        {
            debug_info("check for anonymous struct/union declaration");
            // Resolve the type specifier to see if it's a struct/union
            TypeDescriptor const * nested_type = resolve_type_descriptor(ctx, specifier_qualifier_list, NULL);

            if (nested_type && (nested_type->kind == NCC_TYPE_KIND_STRUCT || nested_type->kind == NCC_TYPE_KIND_UNION))
            {
                debug_info(
                    "Found anonymous nested struct/union - flattening %zu members",
                    nested_type->struct_metadata.members.num_members
                );

                if (num_members + nested_type->struct_metadata.members.num_members >= max_num_members)
                {
                    debug_info("reallocing members was: %p", (void *)members);
                    max_num_members += nested_type->struct_metadata.members.num_members;
                    members = realloc(members, max_num_members * sizeof(*members));
                    if (members == NULL)
                    {
                        debug_error("malloc failure");
                        return object_members;
                    }
                    debug_info("realloced members now: %p", (void *)members);
                }

                // Pull members from the nested type into the current list
                for (size_t m = 0; m < nested_type->struct_metadata.members.num_members; m++)
                {
                    // Copy the member descriptor
                    struct_field_t const * nested_mem = &nested_type->struct_metadata.members.members[m];

                    // Note: You may need to adjust storage_index or offsets here
                    // depending on how your LLVM struct builder handles flattening.
                    struct_field_t new_member = *nested_mem;
                    new_member.name = strdup(nested_mem->name);

                    unsigned type_bits;
                    struct_field_t * previous_member = NULL;
                    if (num_members > 0)
                    {
                        previous_member = &members[num_members - 1];
                        type_bits = LLVMGetIntTypeWidth(previous_member->type_desc->llvm_type);
                    }
                    else
                    {
                        type_bits = LLVMGetIntTypeWidth(nested_mem->type_desc->llvm_type);
                    }
                    if (m == 0 || previous_member == NULL
                        || (strlen(nested_mem->name) > 0 && nested_mem->bitfield.bit_width == 0)
                        || (strlen(previous_member->name) == 0 && previous_member->bitfield.bit_width == 0)
                        || nested_mem->bitfield.bit_width + previous_member->bitfield.bit_offset
                                   + previous_member->bitfield.bit_width
                               > type_bits)
                    {
                        if (previous_member == NULL)
                        {
                            debug_warning("1");
                            new_member.bitfield.storage_index = 0;
                        }
                        else if (m > 0 && nested_type->kind == NCC_TYPE_KIND_UNION)
                        {
                            debug_warning("2: m: %zu, %u", m, previous_member->bitfield.storage_index);
                            new_member.bitfield.storage_index = previous_member->bitfield.storage_index;
                        }
                        else
                        {
                            debug_warning("3: m: %zu, %u", m, previous_member->bitfield.storage_index);
                            new_member.bitfield.storage_index
                                = (previous_member == NULL) ? 0 : (previous_member->bitfield.storage_index + 1);
                        }
                    }
                    else
                    {
                        debug_warning("4: m: %zu, %u", m, previous_member->bitfield.storage_index);
                        new_member.bitfield.storage_index = previous_member->bitfield.storage_index;
                        new_member.bitfield.bit_offset
                            = previous_member->bitfield.bit_offset + previous_member->bitfield.bit_width;
                    }
                    debug_info("adding member: %s %p at index: %u", new_member.name, new_member.name, num_members);
                    members[num_members] = new_member;
                    num_members++;
                }

                continue; // Move to the next member in the parent struct
            }

            debug_info("No declarator and not an anonymous compound type - skipping");
            continue;
        }

        c_grammar_node_t const * struct_decl_node = declarator_list->list.children[0];

        struct_field_t new_member = {0};

        if (struct_decl_node->type == AST_NODE_STRUCT_DECLARATOR && struct_decl_node->list.count > 0)
        {
            c_grammar_node_t * decl = struct_decl_node->list.children[0];

            if (decl->type == AST_NODE_STRUCT_DECLARATOR_BITFIELD)
            {
                if (decl->list.count < 1 || decl->list.count > 2)
                {
                    continue;
                }
                size_t width_idx;
                if (decl->list.count == 1)
                {
                    width_idx = 0;
                    new_member.name = strdup("");
                }
                else
                {
                    width_idx = 1;
                    c_grammar_node_t const * bf_decl = decl->list.children[0];
                    if (bf_decl->type == AST_NODE_DECLARATOR)
                    {
                        c_grammar_node_t const * direct_decl = bf_decl->declarator.direct_declarator;
                        if (direct_decl && direct_decl->list.count > 0)
                        {
                            c_grammar_node_t * ident = direct_decl->list.children[0];
                            if (ident && ident->type == AST_NODE_IDENTIFIER && ident->text != NULL)
                            {
                                new_member.name = strdup(ident->text);
                            }
                        }
                    }
                }
                c_grammar_node_t * width_node = decl->list.children[width_idx];
                if (width_node->type == AST_NODE_INTEGER_LITERAL)
                {
                    new_member.bitfield.bit_width = (unsigned)width_node->integer_lit.integer_literal.value;
                }

                debug_info("resolving member: %s", new_member.name);
                new_member.type_desc = resolve_type_descriptor(ctx, type_spec, NULL);
                if (new_member.type_desc == NULL)
                {
                    debug_info("%s failed to get type descriptor", __func__);
                    free(new_member.name);
                    continue;
                }
                debug_info("resolved: %p", new_member.type_desc);

                unsigned type_bits;
                struct_field_t * previous_member = NULL;
                if (num_members > 0)
                {
                    previous_member = &members[num_members - 1];
                    type_bits = LLVMGetIntTypeWidth(previous_member->type_desc->llvm_type);
                }
                else
                {
                    type_bits = LLVMGetIntTypeWidth(new_member.type_desc->llvm_type);
                }
                if (previous_member == NULL || (strlen(new_member.name) > 0 && new_member.bitfield.bit_width == 0)
                    || (strlen(previous_member->name) == 0 && previous_member->bitfield.bit_width == 0)
                    || LLVMGetTypeKind(new_member.type_desc->llvm_type)
                           != LLVMGetTypeKind(previous_member->type_desc->llvm_type)
                    || new_member.bitfield.bit_width + previous_member->bitfield.bit_offset
                               + previous_member->bitfield.bit_width
                           > type_bits)
                {
                    new_member.bitfield.storage_index
                        = (previous_member == NULL) ? 0 : (previous_member->bitfield.storage_index + 1);
                }
                else
                {
                    new_member.bitfield.storage_index = previous_member->bitfield.storage_index;
                    new_member.bitfield.bit_offset
                        = previous_member->bitfield.bit_offset + previous_member->bitfield.bit_width;
                }
                debug_info("adding member: %s %p at index: %u", new_member.name, new_member.name, num_members);
                members[num_members] = new_member;
                num_members++;
            }
            else if (decl->type == AST_NODE_DECLARATOR)
            {
                debug_info("resolving declarator");
                new_member.type_desc = resolve_type_descriptor(ctx, type_spec, decl);
                debug_info("resolved: %p", new_member.type_desc);

                if (new_member.type_desc == NULL)
                {
                    continue;
                }

                c_grammar_node_t const * direct_decl = decl->declarator.direct_declarator;
                if (direct_decl->list.count > 0)
                {
                    c_grammar_node_t * ident = direct_decl->list.children[0];
                    if (ident && ident->type == AST_NODE_IDENTIFIER && ident->text != NULL)
                    {
                        new_member.name = strdup(ident->text);
                    }
                }
                if (new_member.name == NULL)
                {
                    continue;
                }

                struct_field_t * previous_member = NULL;
                if (num_members > 0)
                {
                    previous_member = &members[num_members - 1];
                }
                new_member.bitfield.storage_index
                    = (previous_member == NULL) ? 0 : (previous_member->bitfield.storage_index + 1);

                debug_info("adding member: %s %p at index: %u", new_member.name, new_member.name, num_members);
                members[num_members] = new_member;
                num_members++;
            }
        }
    }

    object_members.members = members;
    object_members.num_members = num_members;

    debug_info("%s: got %zu members", __func__, object_members.num_members);
    if (debug_get_level() >= DEBUG_LEVEL_INFO)
    {
        for (size_t i = 0; i < object_members.num_members; i++)
        {
            fprintf(
                stderr,
                "member %zu: %s offset: %u, width: %u, storage: %u\n",
                i,
                object_members.members[i].name,
                object_members.members[i].bitfield.bit_offset,
                object_members.members[i].bitfield.bit_width,
                object_members.members[i].bitfield.storage_index
            );
        }
    }

    return object_members;
}
