#include "type_specifier.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

void
type_specifier_dump(TypeSpecifier spec, debug_level_t level)
{
    if (level < debug_get_level())
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
        if (spec.long_count > 0 || spec.is_char || spec.is_int || spec.is_float || spec.is_double || spec.is_void
            || spec.is_bool)
        {
            return false;
        }
    }

    return true;
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
        spec->is_int = true;
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
