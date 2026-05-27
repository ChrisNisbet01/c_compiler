#include "type_qualifiers.h"

#include <stdio.h>
#include <string.h>

static TypeQualifier
append_type_qualifiers(c_grammar_node_t const * qual_list, TypeQualifier quals)
{
    if (qual_list == NULL)
    {
        return quals;
    }

    for (size_t i = 0; i < qual_list->list.count; ++i)
    {
        c_grammar_node_t * child = qual_list->list.children[i];
        if (child->text == NULL)
        {
            continue;
        }

        if (strcmp(child->text, "const") == 0)
        {
            quals.is_const = true;
        }
        else if (strcmp(child->text, "volatile") == 0)
        {
            quals.is_volatile = true;
        }
    }

    return quals;
}

TypeQualifier
build_type_qualifiers(c_grammar_node_t const * qual_list)
{
    return append_type_qualifiers(qual_list, (TypeQualifier){0});
}

TypeQualifier
build_type_qualifiers_from_parent(c_grammar_node_t const * parent)
{
    TypeQualifier quals = {0};

    if (parent == NULL)
    {
        return quals;
    }

    for (size_t i = 0; i < parent->list.count; i++)
    {
        c_grammar_node_t * child = parent->list.children[i];
        if (child->type == AST_NODE_TYPE_QUALIFIERS)
        {
            quals = append_type_qualifiers(child, quals);
        }
    }

    return quals;
}