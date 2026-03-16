#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    AST_NODE_TRANSLATION_UNIT,
    AST_NODE_FUNCTION_DEFINITION,
    AST_NODE_COMPOUND_STATEMENT,
    AST_NODE_DECLARATION,
    AST_NODE_INTEGER_LITERAL,
    AST_NODE_IDENTIFIER,
    AST_NODE_DECL_SPECIFIERS,
    AST_NODE_ASSIGNMENT,
} c_grammar_node_type_t;

typedef struct c_grammar_node_t {
    c_grammar_node_type_t type;
    union {
        struct {
            struct c_grammar_node_t ** children;
            size_t count;
        } list;
        struct {
            char * text;
            long value;
        } terminal;
    } data;
} c_grammar_node_t;

void
c_grammar_node_free(void * node, void * user_data);
