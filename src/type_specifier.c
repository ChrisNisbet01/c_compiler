#include "type_specifier.h"

#include "ast_node_name.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

void
type_specifier_dump(TypeSpecifier spec, debug_level_t level)
{
    if (!debug_is_enabled(level))
    {
        return;
    }

    fprintf(
        stderr,
        "Specifiers:\n\tunsigned: %d\n\tsigned: %d\n\tlong: %u\n\tint %d\n\tvoid: %d\n\tbool: %d\n\tshort: %d\n\tchar: "
        "%d\n\tfloat %d\n\tdouble %d\n",
        spec.is_unsigned,
        spec.is_signed,
        spec.long_count,
        spec.is_int,
        spec.is_void,
        spec.is_bool,
        spec.is_short,
        spec.is_char,
        spec.is_float,
        spec.is_double
    );
}

bool
type_specifier_is_valid(TypeSpecifier const spec)
{
    if (spec.long_count > 2)
    {
        return false;
    }
    if (spec.is_signed && spec.is_unsigned)
    {
        return false;
    }
    if (spec.is_double)
    {
        if (spec.is_signed || spec.is_unsigned || spec.is_void || spec.is_float || spec.is_bool || spec.is_char
            || spec.is_short || spec.is_int || spec.is_float)
        {
            return false;
        }
        if (spec.long_count > 1)
        {
            return false;
        }
    }
    if (spec.is_float)
    {
        if (spec.long_count > 0 || spec.is_signed || spec.is_unsigned || spec.is_void || spec.is_bool || spec.is_char
            || spec.is_short || spec.is_int)
        {
            return false;
        }
    }
    if (spec.is_char)
    {
        if (spec.long_count > 0 || spec.is_short || spec.is_int || spec.is_float || spec.is_double || spec.is_void
            || spec.is_bool)
        {
            return false;
        }
    }
    if (spec.is_short)
    {
        if (spec.long_count > 0 || spec.is_char || spec.is_float || spec.is_double || spec.is_void || spec.is_bool)
        {
            return false;
        }
    }

    return true;
}

TypeSpecifierValidationResult
validate_type_specifiers(c_grammar_node_t const * type_specifiers)
{
    TypeSpecifierValidationResult result = {0};
    if (type_specifiers == NULL
        || (type_specifiers->type != AST_NODE_TYPE_SPECIFIERS
            && type_specifiers->type != AST_NODE_STRUCT_SPECIFIER_QUALIFIER_LIST))
    {
        debug_warning("%s: bad type: %s", __func__, get_node_type_name_from_node(type_specifiers));
        return result;
    }

    if (type_specifiers->list.count == 1)
    {
        c_grammar_node_t const * specifier = type_specifiers->list.children[0];
        if (specifier->type == AST_NODE_TYPEDEF_SPECIFIER_QUALIFIER)
        {
            /* Not really accurate, but it will do. */
            result.is_struct_or_union_or_enum = true;
        }
        else if (specifier->list.count == 1)
        {
            c_grammar_node_t const * inner = specifier->list.children[0];
            if (inner->type == AST_NODE_STRUCT_DEFINITION || inner->type == AST_NODE_UNION_DEFINITION
                || inner->type == AST_NODE_ENUM_DEFINITION || inner->type == AST_NODE_STRUCT_TYPE_REF
                || inner->type == AST_NODE_UNION_TYPE_REF || inner->type == AST_NODE_ENUM_TYPE_REF
                || inner->type == AST_NODE_TYPEDEF_SPECIFIER)
            {
                result.is_struct_or_union_or_enum = true;
            }
            else if (inner->type == AST_NODE_TYPEOF_SPECIFIER)
            {
                result.is_typeof = true;
            }
        }
    }

    result.is_native_type = true;
    for (size_t i = 0; i < type_specifiers->list.count; ++i)
    {
        c_grammar_node_t const * specifier = type_specifiers->list.children[i];
        if (specifier->text == NULL)
        {
            result.is_native_type = false;
            break;
        }
    }
    // Valid if exactly one of is_native, is_struct_or_union_or_enum or is_typeof is true
    int num_true = 0;

    if (result.is_native_type)
    {
        num_true++;
    }
    if (result.is_struct_or_union_or_enum)
    {
        num_true++;
    }
    if (result.is_typeof)
    {
        num_true++;
    }

    result.is_valid = num_true == 1;

    return result;
}

static void
type_specifier_process_specifier(TypeSpecifier * spec, char const * specifier)
{
    if (specifier == NULL)
    {
        return;
    }

    if (strcmp(specifier, "unsigned") == 0)
    {
        spec->is_unsigned = true;
    }
    else if (strcmp(specifier, "signed") == 0)
    {
        spec->is_signed = true;
    }
    else if (strcmp(specifier, "int") == 0)
    {
        spec->is_int = true;
    }
    else if (strcmp(specifier, "long") == 0)
    {
        spec->long_count++;
    }
    else if (strcmp(specifier, "short") == 0)
    {
        spec->is_short = true;
    }
    else if (strcmp(specifier, "char") == 0)
    {
        spec->is_char = true;
    }
    else if (strcmp(specifier, "float") == 0)
    {
        spec->is_float = true;
    }
    else if (strcmp(specifier, "double") == 0)
    {
        spec->is_double = true;
    }
    else if (strcmp(specifier, "bool") == 0 || strcmp(specifier, "_Bool") == 0)
    {
        spec->is_bool = true;
    }
    else if (strcmp(specifier, "void") == 0)
    {
        spec->is_void = true;
    }
}

TypeSpecifier
build_type_specifiers(c_grammar_node_t const * spec_list)
{
    TypeSpecifier spec = {0};
    if (spec_list == NULL
        || (spec_list->type != AST_NODE_TYPE_SPECIFIERS && spec_list->type != AST_NODE_STRUCT_SPECIFIER_QUALIFIER_LIST))
    {
        return spec;
    }
    TypeSpecifierValidationResult validation_result = validate_type_specifiers(spec_list);

    if (!validation_result.is_valid)
    {
        debug_info("Invalid type specifiers in declaration");
        return (TypeSpecifier){0};
    }
    if (!validation_result.is_native_type)
    {
        debug_info("Not a native type, cannot build TypeSpecifier");
        return (TypeSpecifier){0};
    }

    debug_info("%s count %u", __func__, spec_list->list.count);
    for (size_t i = 0; i < spec_list->list.count; ++i)
    {
        c_grammar_node_t * child = spec_list->list.children[i];

        type_specifier_process_specifier(&spec, child->text);
    }

    debug_info("%s", __func__);
    type_specifier_dump(spec, DEBUG_LEVEL_INFO);

    return spec;
}

TypeSpecifier
build_type_specifiers_from_declaration_specifiers(c_grammar_node_t const * declaration_specs)
{
    if (declaration_specs == NULL || declaration_specs->type != AST_NODE_NAMED_DECL_SPECIFIERS)
    {
        return (TypeSpecifier){0};
    }
    c_grammar_node_t const * type_spec_list = declaration_specs->decl_specifiers.type_specifiers;

    return build_type_specifiers(type_spec_list);
}
