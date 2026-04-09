#include "ast_print.h"

#include "ast_node_name.h"

#include <stdio.h>

static void print_ast_internal(c_grammar_node_t const * node, int indent);

static void
print_list_type_ast_node(c_grammar_node_t const * node, int indent)
{
    for (size_t i = 0; i < node->list.count; i++)
        print_ast_internal(node->list.children[i], indent + 1);
}

static void
print_indent(unsigned int indent)
{
    fprintf(stderr, "%*s", indent * 2, "");
}

static void
print_ast_internal(c_grammar_node_t const * node, int indent)
{
    if (node == NULL)
    {
        fprintf(stderr, "NULL node\n");
        return;
    }

    print_indent(indent);
    fprintf(stderr, "%s (%u):", get_node_type_name_from_node(node), node->type);
    if (node->text != NULL)
    {
        fprintf(stderr, " `%s`", node->text);
    }
    fprintf(stderr, " (%zu child%s)\n", node->list.count, (node->list.count == 1) ? "" : "ren");

    print_list_type_ast_node(node, indent);
}

void
print_ast(c_grammar_node_t const * node)
{
    fprintf(stderr, "AST:\n");
    print_ast_internal(node, 0);
}
