#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum
{
    AST_NODE_TRANSLATION_UNIT,
    AST_NODE_FUNCTION_DEFINITION,
    AST_NODE_COMPOUND_STATEMENT,
    AST_NODE_DECLARATION,
    AST_NODE_INTEGER_BASE,
    AST_NODE_FLOAT_BASE,
    AST_NODE_INTEGER_VALUE,
    AST_NODE_FLOAT_VALUE,
    AST_NODE_STRING_LITERAL,
    AST_NODE_LITERAL_SUFFIX,
    AST_NODE_IDENTIFIER,
    AST_NODE_DECL_SPECIFIERS,
    AST_NODE_ASSIGNMENT,
    AST_NODE_TYPE_SPECIFIER,
    AST_NODE_BINARY_OP,
    AST_NODE_UNARY_OP,
    AST_NODE_OPERATOR,
    AST_NODE_DECLARATOR,
    AST_NODE_DIRECT_DECLARATOR,
    AST_NODE_DECLARATOR_SUFFIX,
    AST_NODE_POINTER,
    AST_NODE_RELATIONAL_EXPRESSION,
    AST_NODE_EQUALITY_EXPRESSION,
    AST_NODE_BITWISE_EXPRESSION,
    AST_NODE_LOGICAL_AND_EXPRESSION,
    AST_NODE_LOGICAL_OR_EXPRESSION,
    AST_NODE_SHIFT_EXPRESSION,
    AST_NODE_ARITHMETIC_EXPRESSION,
    AST_NODE_FUNCTION_CALL,
    AST_NODE_POSTFIX_EXPRESSION,
    AST_NODE_ARRAY_SUBSCRIPT,
    AST_NODE_MEMBER_ACCESS_DOT,
    AST_NODE_MEMBER_ACCESS_ARROW,
    AST_NODE_CAST_EXPRESSION,
    AST_NODE_INIT_DECLARATOR,
    AST_NODE_IF_STATEMENT,
    AST_NODE_SWITCH_STATEMENT,
    AST_NODE_WHILE_STATEMENT,
    AST_NODE_DO_WHILE_STATEMENT,
    AST_NODE_FOR_STATEMENT,
    AST_NODE_GOTO_STATEMENT,
    AST_NODE_CONTINUE_STATEMENT,
    AST_NODE_BREAK_STATEMENT,
    AST_NODE_RETURN_STATEMENT,
    AST_NODE_TYPE_NAME,
    AST_NODE_EXPRESSION_STATEMENT,
    AST_NODE_STRUCT_DEFINITION,
    AST_NODE_INITIALIZER_LIST,
    AST_NODE_LABELED_STATEMENT,
    AST_NODE_CHARACTER_LITERAL,
    AST_NODE_CASE_STATEMENT,
    AST_NODE_DEFAULT_STATEMENT,
    AST_NODE_LABELED_IDENTIFIER,
} c_grammar_node_type_t;

typedef struct
{
    struct c_grammar_node_t ** children;
    size_t count;
} ast_node_list_t;

typedef enum
{
    BITWISE_OP_AND,
    BITWISE_OP_XOR,
    BITWISE_OP_OR,
} bitwise_operator_type_t;

typedef enum
{
    SHIFT_OP_LL,  // <<
    SHIFT_OP_AR,  // >>
} shift_operator_type_t;

typedef enum
{
    ARITH_OP_ADD,   // +
    ARITH_OP_SUB,   // -
    ARITH_OP_MUL,   // *
    ARITH_OP_DIV,   // /
    ARITH_OP_MOD,   // %
} arithmetic_operator_type_t;

typedef enum
{
    REL_OP_LT,   // <
    REL_OP_GT,   // >
    REL_OP_LE,   // <=
    REL_OP_GE,   // >=
} relational_operator_type_t;

typedef enum
{
    EQ_OP_EQ,   // ==
    EQ_OP_NE,   // !=
} equality_operator_type_t;

typedef enum
{
    LOGICAL_OP_AND,   // &&
    LOGICAL_OP_OR,    // ||
} logical_operator_type_t;

typedef struct
{
    bitwise_operator_type_t op;
} bitwise_operator_data_t;

typedef struct
{
    shift_operator_type_t op;
} shift_operator_data_t;

typedef struct
{
    arithmetic_operator_type_t op;
} arithmetic_operator_data_t;

typedef struct
{
    relational_operator_type_t op;
} relational_operator_data_t;

typedef struct
{
    equality_operator_type_t op;
} equality_operator_data_t;

typedef struct
{
    logical_operator_type_t op;
} logical_operator_data_t;

typedef struct c_grammar_node_t
{
    c_grammar_node_type_t type;
    bool is_terminal_node;
    union
    {
        ast_node_list_t list; /* TODO: rename to children. */
        struct
        {
            char * text;
        } terminal;
    } data;
    union
    {
        bitwise_operator_data_t bitwise_op;
        shift_operator_data_t shift_op;
        arithmetic_operator_data_t arith_op;
        relational_operator_data_t rel_op;
        equality_operator_data_t eq_op;
        logical_operator_data_t logical_op;
    };

} c_grammar_node_t;

void c_grammar_node_free(void * node, void * user_data);
