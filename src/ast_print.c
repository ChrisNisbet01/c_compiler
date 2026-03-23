#include "ast_print.h"

#include "ast_node_name.h"

#include <stdio.h>

static void print_ast_internal(c_grammar_node_t const * node, int indent);

static void
print_list_type_ast_node(c_grammar_node_t const * node, int indent)
{
    printf("%s (%u): (%zu children)\n", get_node_type_name_from_node(node), node->type, node->data.list.count);
    for (size_t i = 0; i < node->data.list.count; i++)
        print_ast_internal(node->data.list.children[i], indent + 1);
}

static void
print_indent(unsigned int indent)
{
    printf("%*s", indent * 2, "");
}

static void
print_ast_internal(c_grammar_node_t const * node, int indent)
{
    if (node == NULL)
    {
        printf("NULL node\n");
        return;
    }
    print_indent(indent);

    if (node->is_terminal_node)
    {
        printf("%s (%u): %s\n", get_node_type_name_from_node(node), node->type, node->data.terminal.text);
        if (node->lhs != NULL)
        {
            print_indent(indent + 1);
            printf("LHS: \n");
            print_ast_internal(node->lhs, indent + 2);
        }
        if (node->op.text != NULL)
        {
            print_indent(indent + 1);
            printf("Operator: %s\n", node->op.text);
        }
        if (node->rhs != NULL)
        {
            print_indent(indent + 1);
            printf("RHS: \n");
            print_ast_internal(node->rhs, indent + 2);
        }
    }
    else
    {
        print_list_type_ast_node(node, indent);
    }
}

void
print_ast(c_grammar_node_t const * node)
{
    print_ast_internal(node, 0);
}
