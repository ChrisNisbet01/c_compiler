#include "type_qualifiers.h"

#include <stdio.h>
#include <string.h>

void
type_qualifiers_dump(TypeQualifier quals, debug_level_t level)
{
    if (level < debug_get_level())
    {
        return;
    }

    fprintf(stderr, "Qualifiers:\n\tconst: %d\n\tvolatile: %d\n", quals.is_const, quals.is_volatile);
}

TypeQualifier
build_type_qualifiers(c_grammar_node_t const * qual_list)
{
    TypeQualifier quals = {0};

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
