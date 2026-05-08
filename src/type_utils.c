#include "type_utils.h"

#include "ast_node_name.h"
#include "debug.h"
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

bool
function_signatures_match(LLVMTypeRef type1, LLVMTypeRef type2)
{
    return type1 == type2;
}
