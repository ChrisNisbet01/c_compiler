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
resolve_array_suffix(ir_generator_ctx_t * ctx, TypeDescriptor const * element_type, c_grammar_node_t const * suffix);

TypeDescriptor const *
resolve_type_descriptor(ir_generator_ctx_t * ctx, c_grammar_node_t const * specifiers, c_grammar_node_t * declarator);

static TypeDescriptor const *
resolve_array_suffix(ir_generator_ctx_t * ctx, TypeDescriptor const * element_type, c_grammar_node_t const * suffix)
{
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
resolve_type_descriptor(ir_generator_ctx_t * ctx, c_grammar_node_t const * specifiers, c_grammar_node_t * declarator)
{
    if (specifiers == NULL)
    {
        return NULL;
    }

    c_grammar_node_t const * type_spec_list = NULL;
    c_grammar_node_t const * type_qual_list = NULL;

    if (specifiers->type == AST_NODE_NAMED_DECL_SPECIFIERS)
    {
        type_spec_list = specifiers->decl_specifiers.type_specifiers;
        type_qual_list = specifiers->decl_specifiers.type_qualifiers;
    }

    TypeDescriptor const * current = NULL;

    if (type_spec_list != NULL && type_spec_list->list.count > 0)
    {
        c_grammar_node_t const * type_spec_node = type_spec_list->list.children[0];

        if (type_spec_node->list.count > 0)
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
            else if (inner->type == AST_NODE_TYPE_SPECIFIER)
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
            }
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
    for (size_t i = 0; i < suffix_list->list.count; i++)
    {
        c_grammar_node_t const * suffix = suffix_list->list.children[i];
        if (is_function_suffix(suffix))
        {
            current = resolve_function_pointer_type(ctx, current, suffix);
        }
        else
        {
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

TypeDescriptor const *
resolve_type_from_ast(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    debug_info("%s: node type %s", __func__, get_node_type_name_from_node(node));

    if (node == NULL || node->type != AST_NODE_DECLARATION)
    {
        return NULL;
    }

    // unsigned int i, j;
    // ^^^^^^^^^^^^
    c_grammar_node_t const * decl_specs = node->declaration.declaration_specifiers;
    c_grammar_node_t const * type_specifiers = decl_specs->decl_specifiers.type_specifiers;
    c_grammar_node_t const * type_qualifiers = decl_specs->decl_specifiers.type_qualifiers;
    TypeQualifier quals = build_type_qualifiers(type_qualifiers);
    TypeSpecifierValidationResult validation_result = validate_type_specifiers(type_specifiers);
    if (!validation_result.is_valid)
    {
        ir_gen_error(&ctx->errors, "Neither struct/union/enum/typedef nor native type specified in declaration");
        return NULL;
    }

    // unsigned int i, j;
    //              ^^^^
    c_grammar_node_t const * init_decl_list = node->declaration.init_declarator_list;

    TypeSpecifier specs = {0};

    if (validation_result.is_native_type)
    {
        debug_info("Processing native type specifiers");

        specs = build_type_specifiers(type_specifiers);
        if (!type_specifier_is_valid(specs))
        {
            ir_gen_error(&ctx->errors, "Invalid combination of type specifiers in declaration");
            type_specifier_dump(specs, DEBUG_LEVEL_ERROR);
            return NULL;
        }
        if (init_decl_list == NULL)
        {
            /* No declarators - just register the type. */
            debug_info("No declarators - not registering the native type");
            return NULL;
        }
    }
    else
    {
        debug_info("Processing struct/union/enum/typedef type specifiers - not implemented yet");
        /* Must be struct/union/enum/typedef. */
        // TODO.
        debug_error("need struct/union/enum/typedef handling in resolve_type_from_ast");
        return NULL;
    }

    TypeDescriptor const * current = NULL;
    for (size_t i = 0; i < init_decl_list->list.count; ++i)
    {
        c_grammar_node_t const * init_decl = init_decl_list->list.children[i];
        c_grammar_node_t const * declarator = init_decl->init_declarator.declarator;
        c_grammar_node_t const * pointer_list = declarator->declarator.pointer_list;
        c_grammar_node_t const * direct_declarator = declarator->declarator.direct_declarator;

        /* FIXME: This is a hack to work around the broken grammar. */
        TypeDescriptor const * base;
        if (pointer_list->list.count == 0)
        {
            base = get_or_create_builtin_type(ctx->type_descriptors, specs, quals);
        }
        else
        {
            c_grammar_node_t const * pointer = pointer_list->list.children[pointer_list->list.count - 1];
            TypeQualifier ptr_quals = build_type_qualifiers(pointer->list.children[0]);
            base = get_or_create_builtin_type(ctx->type_descriptors, specs, ptr_quals);
        }
        current = base;

        // TODO: Declare the variable with the resolved type, and handle pointer levels and direct declarator suffixes
        // (arrays/functions)

        // 3. Handle Pointer levels (if applicable in this node)
        // C handles pointers from right to left in the AST declarator
        /* FIXME - The grammar doesn't handle pointer levels correctly. When there are pointers, the pointer specifiers
         * are attached to the incorrect pointer level. */
        for (size_t i = pointer_list->list.count; i > 0; i--)
        {
            TypeQualifier ptr_quals;

            if (i > 1)
            {
                c_grammar_node_t const * prev_pointer = pointer_list->list.children[i - 2];
                // If node is 'int * const', extract 'const' for the pointer level
                c_grammar_node_t const * prev_pointer_qual_list = prev_pointer->list.children[0];
                ptr_quals = build_type_qualifiers(prev_pointer_qual_list);
            }
            else
            {
                ptr_quals = quals;
            }
            current = get_or_create_pointer_type(ctx->type_descriptors, current, ptr_quals);
        }
    }

    return current; /* BUG: This will return the last resolved type in a situation like `int p1, p2;`. */
}
