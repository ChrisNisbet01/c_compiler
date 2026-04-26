#include "declaration_handler.h"

#include "ast_node_name.h"
#include "debug.h"

#include <stdio.h>
#include <string.h>

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

    bool is_struct_or_union_or_enum_or_typedef = false;
    bool is_native_type = true;
    if (type_specifiers->list.count == 1)
    {
        c_grammar_node_t const * specifier = type_specifiers->list.children[0];
        if (specifier->list.count == 1)
        {
            c_grammar_node_t const * child = specifier->list.children[0];
            if (child->type == AST_NODE_STRUCT_DEFINITION || child->type == AST_NODE_UNION_DEFINITION
                || child->type == AST_NODE_ENUM_DEFINITION || child->type == AST_NODE_STRUCT_TYPE_REF
                || child->type == AST_NODE_UNION_TYPE_REF || child->type == AST_NODE_ENUM_TYPE_REF)
            {
                is_struct_or_union_or_enum_or_typedef = true;
            }
        }
    }
    for (size_t i = 0; i < type_specifiers->list.count; ++i)
    {
        c_grammar_node_t const * specifier = type_specifiers->list.children[i];
        if (specifier->text == NULL)
        {
            is_native_type = false;
            break;
        }
    }
    if (!is_struct_or_union_or_enum_or_typedef && !is_native_type)
    {
        ir_gen_error(&ctx->errors, "Neither struct/union/enum/typedef nor native type specified in declaration");
        return NULL;
    }

    // unsigned int i, j;
    //              ^^^^
    c_grammar_node_t const * init_decl_list = node->declaration.init_declarator_list;

    TypeSpecifier specs = {0};

    if (is_native_type)
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
