#include "ast_print.h"

#include "ast_node_name.h"

#include <stdio.h>

static void print_indent(unsigned int indent, FILE * stream);

static void print_ast_internal(c_grammar_node_t const * node, int indent, FILE * stream);

static void
print_list_type_ast_node(c_grammar_node_t const * node, int indent, FILE * stream)
{
    for (size_t i = 0; i < node->list.count; i++)
    {
        print_ast_internal(node->list.children[i], indent + 1, stream);
    }
}

static void
print_indent(unsigned int indent, FILE * stream)
{
    fprintf(stream, "%*s", indent * 2, "");
}

static void
print_ast_internal(c_grammar_node_t const * node, int indent, FILE * stream)
{
    if (node == NULL)
    {
        fprintf(stream, "NULL node\n");
        return;
    }

    print_indent(indent, stream);
    fprintf(stream, "%s (%u):", get_node_type_name_from_node(node), node->type);
    if (node->text != NULL)
    {
        fprintf(stream, " `%s`", node->text);
    }
    fprintf(stream, " (%zu child%s)\n", node->list.count, (node->list.count == 1) ? "" : "ren");
    print_list_type_ast_node(node, indent, stream);
}

void
print_ast(c_grammar_node_t const * node)
{
    print_ast_to_stream(node, stderr);
}

void
print_ast_to_stream(c_grammar_node_t const * node, FILE * stream)
{
    fprintf(stream, "AST:\n");
    print_ast_internal(node, 0, stream);
}

void
print_ast_with_label(c_grammar_node_t const * node, char const * label)
{
    print_ast_with_label_to_stream(node, label, stderr);
}

void
print_ast_with_label_to_stream(c_grammar_node_t const * node, char const * label, FILE * stream)
{
    fprintf(stream, "AST: %s:\n", label);
    print_ast_internal(node, 0, stream);
}
