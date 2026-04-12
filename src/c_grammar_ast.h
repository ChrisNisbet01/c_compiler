#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct c_grammar_node_t c_grammar_node_t;

typedef enum
{
    AST_NODE_TRANSLATION_UNIT,
    AST_NODE_EXTERNAL_DECLARATIONS,
    AST_NODE_EXTERNAL_DECLARATION,
    AST_NODE_TOP_LEVEL_DECLARATION,
    AST_NODE_PREPROCESSOR_DIRECTIVE,
    AST_NODE_FUNCTION_DEFINITION,
    AST_NODE_COMPOUND_STATEMENT,
    AST_NODE_ASM_STATEMENT,
    AST_NODE_OPTIONAL_KW_EXTENSION,
    AST_NODE_INIT_DECLARATOR_LIST,
    AST_NODE_DECLARATION,
    AST_NODE_INTEGER_BASE,
    AST_NODE_FLOAT_BASE,
    AST_NODE_INTEGER_LITERAL,
    AST_NODE_FLOAT_LITERAL,
    AST_NODE_STRING_LITERAL,
    AST_NODE_LITERAL_SUFFIX,
    AST_NODE_IDENTIFIER,
    AST_NODE_NAMED_DECL_SPECIFIERS,
    AST_NODE_ASSIGNMENT,
    AST_NODE_TYPE_SPECIFIER,
    AST_NODE_TYPEDEF_SPECIFIER,
    AST_NODE_UNARY_OPERATOR,
    AST_NODE_UNARY_EXPRESSION_PREFIX,
    AST_NODE_DECLARATOR,
    AST_NODE_DIRECT_DECLARATOR,
    AST_NODE_DECLARATOR_SUFFIX,
    AST_NODE_DECLARATOR_SUFFIX_LIST,
    AST_NODE_POINTER,
    AST_NODE_POINTER_LIST,
    AST_NODE_FUNCTION_POINTER_DECLARATOR,
    AST_NODE_RELATIONAL_OPERATOR,
    AST_NODE_RELATIONAL_EXPRESSION,
    AST_NODE_EQUALITY_OPERATOR,
    AST_NODE_EQUALITY_EXPRESSION,
    AST_NODE_BITWISE_OPERATOR,
    AST_NODE_BITWISE_EXPRESSION,
    AST_NODE_LOGICAL_OPERATOR,
    AST_NODE_LOGICAL_EXPRESSION,
    AST_NODE_SHIFT_OPERATOR,
    AST_NODE_SHIFT_EXPRESSION,
    AST_NODE_ARITHMETIC_OPERATOR,
    AST_NODE_ARITHMETIC_EXPRESSION,
    AST_NODE_OPTIONAL_ARGUMENT_LIST,
    AST_NODE_POSTFIX_PARTS,
    AST_NODE_POSTFIX_EXPRESSION,
    AST_NODE_POSTFIX_OPERATOR,
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
    AST_NODE_STRUCT_DECLARATION,
    AST_NODE_STRUCT_DECLARATION_LIST,
    AST_NODE_STRUCT_DEFINITION,
    AST_NODE_UNION_DEFINITION,
    AST_NODE_ENUM_DEFINITION,
    AST_NODE_ENUMERATOR_LIST,
    AST_NODE_STRUCT_TYPE_REF,
    AST_NODE_UNION_TYPE_REF,
    AST_NODE_ENUM_TYPE_REF,
    AST_NODE_ENUMERATOR,
    AST_NODE_TYPEDEF_DECLARATION,
    AST_NODE_TYPEDEF_INIT_DECLARATION_LIST,
    AST_NODE_TYPEDEF_DECLARATOR,
    AST_NODE_TYPEDEF_DIRECT_DECLARATOR,
    AST_NODE_TYPEDEF_INIT_DECLARATOR,
    AST_NODE_INITIALIZER_LIST,
    AST_NODE_INITIALIZER_LIST_ENTRY,
    AST_NODE_INITIALIZER,
    AST_NODE_LABELED_STATEMENT,
    AST_NODE_CHARACTER_LITERAL,
    AST_NODE_CASE_LABEL,
    AST_NODE_CASE_LABELS,
    AST_NODE_SWITCH_CASE,
    AST_NODE_DEFAULT_STATEMENT,
    AST_NODE_SWITCH_BODY_STATEMENTS,
    AST_NODE_ASSIGNMENT_OPERATOR,
    AST_NODE_TERNARY_OPERATION,
    AST_NODE_CONDITIONAL_EXPRESSION,
    AST_NODE_COMMA_EXPRESSION,
    AST_NODE_DESIGNATION,
    AST_NODE_COMPOUND_LITERAL,
    AST_NODE_STRUCT_DECLARATOR,
    AST_NODE_STRUCT_DECLARATOR_LIST,
    AST_NODE_STRUCT_SPECIFIER_QUALIFIER_LIST,
    AST_NODE_STRUCT_DECLARATOR_BITFIELD,
    AST_NODE_ATTRIBUTE,
    AST_NODE_ATTRIBUTE_LIST,
    AST_NODE_ASM_NAMES,
    AST_NODE_ABSTRACT_DECLARATOR,
    AST_NODE_STORAGE_CLASS_SPECIFIER,
    AST_NODE_STORAGE_CLASS_SPECIFIERS,
    AST_NODE_FUNCTION_SPECIFIER,
    AST_NODE_TYPE_QUALIFIER,
    AST_NODE_TYPE_QUALIFERS,
    AST_NODE_DECLARATION_SPECIFIERS,
} c_grammar_node_type_t;

typedef struct
{
    c_grammar_node_t ** children;
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
    SHIFT_OP_LL, // <<
    SHIFT_OP_AR, // >>
} shift_operator_type_t;

typedef enum
{
    ARITH_OP_ADD, // +
    ARITH_OP_SUB, // -
    ARITH_OP_MUL, // *
    ARITH_OP_DIV, // /
    ARITH_OP_MOD, // %
} arithmetic_operator_type_t;

typedef enum
{
    REL_OP_LT, // <
    REL_OP_GT, // >
    REL_OP_LE, // <=
    REL_OP_GE, // >=
} relational_operator_type_t;

typedef enum
{
    EQ_OP_EQ, // ==
    EQ_OP_NE, // !=
} equality_operator_type_t;

typedef enum
{
    LOGICAL_OP_AND, // &&
    LOGICAL_OP_OR,  // ||
} logical_operator_type_t;

typedef enum
{
    UNARY_OP_PLUS,    // + (unary)
    UNARY_OP_MINUS,   // - (unary)
    UNARY_OP_NOT,     // ! (logical not)
    UNARY_OP_BITNOT,  // ~ (bitwise not)
    UNARY_OP_ADDR,    // & (address-of)
    UNARY_OP_DEREF,   // * (dereference)
    UNARY_OP_INC,     // ++ (prefix increment)
    UNARY_OP_DEC,     // -- (prefix decrement)
    UNARY_OP_SIZEOF,  // sizeof
    UNARY_OP_ALIGNOF, // __alignof__
} unary_operator_type_t;

typedef enum
{
    POSTFIX_OP_INC, // ++
    POSTFIX_OP_DEC, // --
} postfix_operator_type_t;

typedef enum
{
    ASSIGN_OP_SIMPLE, // =
    ASSIGN_OP_SHL,    // <<=
    ASSIGN_OP_SHR,    // >>=
    ASSIGN_OP_ADD,    // +=
    ASSIGN_OP_SUB,    // -=
    ASSIGN_OP_MUL,    // *=
    ASSIGN_OP_DIV,    // /=
    ASSIGN_OP_MOD,    // %=
    ASSIGN_OP_AND,    // &=
    ASSIGN_OP_XOR,    // ^=
    ASSIGN_OP_OR,     // |=
} assignment_operator_type_t;

typedef enum
{
    STORAGE_CLASS_NONE,
    STORAGE_CLASS_STATIC,
    STORAGE_CLASS_EXTERN,
    STORAGE_CLASS_AUTO,
    STORAGE_CLASS_REGISTER,
} storage_class_type_t;

typedef struct
{
    storage_class_type_t storage_class;
} storage_class_data_t;

typedef struct
{
    bool has_static;
    bool has_extern;
    bool has_auto;
    bool has_register;
} storage_class_specifiers_data_t;

typedef struct
{
    bool is_const;
    bool is_volatile;
    bool is_restrict;
} type_qualifier_data_t;

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

typedef struct
{
    unary_operator_type_t op;
} unary_operator_data_t;

typedef struct
{
    postfix_operator_type_t op;
} postfix_operator_data_t;

typedef struct
{
    assignment_operator_type_t op;
} assignment_operator_data_t;

typedef enum
{
    FLOAT_LITERAL_TYPE_DOUBLE,
    FLOAT_LITERAL_TYPE_FLOAT,
    FLOAT_LITERAL_TYPE_LONG_DOUBLE,
} float_literal_type_t;

typedef struct
{
    long double value;
    float_literal_type_t type; /* Default to double. */
} float_literal_data_t;

typedef struct
{
    long long value;
    bool is_unsigned;
    bool is_long;
} integer_literal_data_t;

typedef struct ast_node_expression_t
{
    c_grammar_node_t const * left;
    c_grammar_node_t const * right;
} ast_node_expression_t;

typedef struct ast_node_float_literal_t
{
    float_literal_data_t float_literal;
} ast_node_float_literal_t;

typedef struct ast_node_integer_literal_t
{
    integer_literal_data_t integer_literal;
} ast_node_integer_literal_t;

typedef struct ast_node_external_declaration_t
{
    /* One or other of the following. */
    c_grammar_node_t const * top_level_declaration;
    c_grammar_node_t const * preprocessor_directive;
} ast_node_external_declaration_t;

typedef struct ast_node_translation_unit_t
{
    c_grammar_node_t const * external_declarations;
} ast_node_translation_unit_t;

typedef struct ast_node_decl_specifiers_t
{
    c_grammar_node_t const * storage_class;
    c_grammar_node_t const * type_qualifiers;
    c_grammar_node_t const * function_specifier;
    c_grammar_node_t const * type_specifier;
    c_grammar_node_t const * typedef_name;
    c_grammar_node_t const * trailing_type_qualifiers;
    bool has_const;
    bool has_volatile;
    bool has_restrict;
    bool has_static;
    bool has_extern;
    bool has_auto;
    bool has_register;
} ast_node_decl_specifiers_t;

typedef struct ast_node_function_definition_t
{
    c_grammar_node_t const * declaration_specifiers;
    c_grammar_node_t const * declarator;
    c_grammar_node_t const * body;
} ast_node_function_definition_t;

typedef struct ast_node_declaration_t
{
    c_grammar_node_t const * extension;
    c_grammar_node_t const * declaration_specifiers;
    c_grammar_node_t const * init_declarator_list;
} ast_node_declaration_t;

typedef struct ast_node_top_level_declaration_t
{
    c_grammar_node_t const * extension;
    c_grammar_node_t const * declaration;
} ast_node_top_level_declaration_t;

typedef struct ast_node_struct_declaration_t
{
    c_grammar_node_t const * extension;
    c_grammar_node_t const * specifier_qualifier_list;
    c_grammar_node_t const * declarator_list;
} ast_node_struct_declaration_t;

typedef struct ast_node_labeled_statement_t
{
    c_grammar_node_t const * label;
    c_grammar_node_t const * statement;
} ast_node_labeled_statement_t;

typedef struct ast_node_if_statement_t
{
    c_grammar_node_t const * condition;
    c_grammar_node_t const * then_statement;
    c_grammar_node_t const * else_statement;
} ast_node_if_statement_t;

typedef struct ast_node_switch_case_t
{
    c_grammar_node_t const * labels;
    c_grammar_node_t const * statements;
} ast_node_switch_case_t;

typedef struct ast_node_switch_t
{
    c_grammar_node_t const * expression;
    c_grammar_node_t const * body;
} ast_node_switch_t;

typedef struct ast_node_type_ref_t
{
    c_grammar_node_t const * attribute_list;
    c_grammar_node_t const * identifier;
} ast_node_type_ref_t;

typedef struct ast_node_loop_statement_t
{
    c_grammar_node_t const * condition;
    c_grammar_node_t const * body;
} ast_node_loop_statement_t;

typedef struct ast_node_do_while_statement_t
{
    c_grammar_node_t const * body;
    c_grammar_node_t const * condition;
} ast_node_do_while_statement_t;

typedef struct ast_node_for_statement_t
{
    c_grammar_node_t const * init;
    c_grammar_node_t const * condition;
    c_grammar_node_t const * post;
    c_grammar_node_t const * body;
} ast_node_for_statement_t;

typedef struct ast_node_goto_statement_t
{
    c_grammar_node_t const * label;
} ast_node_goto_statement_t;

typedef struct ast_node_return_statement_t
{
    c_grammar_node_t const * expression;
} ast_node_return_statement_t;

typedef struct ast_node_expression_statement_t
{
    c_grammar_node_t const * expression;
} ast_node_expression_statement_t;

typedef struct ast_node_typedef_declaration_t
{
    c_grammar_node_t const * extension;
    c_grammar_node_t const * declaration_specifiers;
    c_grammar_node_t const * init_declarator_list;
} ast_node_typedef_declaration_t;

typedef struct ast_node_init_declarator_t
{
    c_grammar_node_t const * declarator;
    c_grammar_node_t const * attribute_list_1;
    c_grammar_node_t const * optional_asm_name_list;
    c_grammar_node_t const * attribute_list_2;
    c_grammar_node_t const * initializer;
} ast_node_init_declarator_t;

typedef struct ast_node_declarator_t
{
    c_grammar_node_t const * pointer_list;
    c_grammar_node_t const * direct_declarator;
    c_grammar_node_t const * declarator_suffix_list;
    c_grammar_node_t const * attribute_list;
} ast_node_declarator_t;

typedef struct ast_node_typedef_declarator_t
{
    c_grammar_node_t const * pointer_list;
    c_grammar_node_t const * direct_declarator;
    c_grammar_node_t const * declarator_suffix_list;
    c_grammar_node_t const * attribute_list;
} ast_node_typedef_declarator_t;

typedef struct ast_node_typedef_direct_declarator_t
{
    c_grammar_node_t const * nested_typedef_declarator;
    c_grammar_node_t const * identifier;
    c_grammar_node_t const * attribute_list;
} ast_node_typedef_direct_declarator_t;

typedef struct ast_node_identifier_t
{
    c_grammar_node_t const * identifier;
} ast_node_identifier_t;

typedef struct ast_node_compound_literal_t
{
    c_grammar_node_t const * type_name;
    c_grammar_node_t const * initializer_list;
} ast_node_compound_literal_t;

typedef struct ast_node_postfix_expression_t
{
    c_grammar_node_t const * base_expression;
    c_grammar_node_t const * postfix_parts;
} ast_node_postfix_expression_t;

typedef struct ast_node_initializer_list_entry_t
{
    c_grammar_node_t const * designation;
    c_grammar_node_t const * initializer;
} ast_node_initializer_list_entry_t;

typedef struct ast_node_enum_definition_t
{
    c_grammar_node_t const * attribute_list_1;
    c_grammar_node_t const * identifier;
    c_grammar_node_t const * enumerator_list;
    c_grammar_node_t const * attribute_list_2;
} ast_node_enum_definition_t;

typedef struct ast_node_struct_definition_t
{
    c_grammar_node_t const * attribute_list_1;
    c_grammar_node_t const * identifier;
    c_grammar_node_t const * declaration_list;
    c_grammar_node_t const * attribute_list_2;
} ast_node_struct_definition_t;

typedef struct ast_node_function_pointer_declarator_t
{
    c_grammar_node_t const * pointer;
    c_grammar_node_t const * identifier;
    c_grammar_node_t const * declarator_suffix_list;
} ast_node_function_pointer_declarator_t;

typedef struct ast_node_enumerator_t
{
    c_grammar_node_t const * identifier;
    c_grammar_node_t const * expression;
} ast_node_enumerator_t;

typedef struct ast_node_unary_expression_prefix_t
{
    c_grammar_node_t const * op;
    c_grammar_node_t const * operand;
} ast_node_unary_expression_prefix_t;

typedef struct ast_node_cast_expression_t
{
    c_grammar_node_t const * type_name;
    c_grammar_node_t const * expression;
} ast_node_cast_expression_t;

typedef struct ast_node_binary_expression_t
{
    c_grammar_node_t const * left;
    c_grammar_node_t const * op;
    c_grammar_node_t const * right;
} ast_node_binary_expression_t;

typedef struct ast_node_ternary_operation_t
{
    c_grammar_node_t const * true_expression;
    c_grammar_node_t const * false_expression;
} ast_node_ternary_operation_t;

typedef struct ast_node_conditional_expression_t
{
    c_grammar_node_t const * condition_expression;
    c_grammar_node_t const * ternary_operation;
} ast_node_conditional_expression_t;

typedef union ast_node_operator_t
{
    bitwise_operator_data_t bitwise;
    shift_operator_data_t shift;
    arithmetic_operator_data_t arith;
    relational_operator_data_t rel;
    equality_operator_data_t eq;
    logical_operator_data_t logical;
    unary_operator_data_t unary;
    postfix_operator_data_t postfix;
    assignment_operator_data_t assign;
} ast_node_operator_t;

typedef struct ast_node_type_name_t
{
    c_grammar_node_t const * specifier_qualifier_list;
    c_grammar_node_t const * abstract_declarator;
} ast_node_type_name_t;

typedef struct c_grammar_node_t
{
    c_grammar_node_type_t type;

    ast_node_list_t list;
    char * text;

    union
    {
        ast_node_expression_t expression;
        ast_node_float_literal_t float_lit;
        ast_node_integer_literal_t integer_lit;
        ast_node_translation_unit_t translation_unit;
        ast_node_external_declaration_t external_declaration;
        ast_node_function_definition_t function_definition;
        ast_node_declaration_t declaration;
        ast_node_decl_specifiers_t decl_specifiers;
        ast_node_top_level_declaration_t top_level_declaration;
        ast_node_struct_declaration_t struct_declaration;
        ast_node_labeled_statement_t labeled_statement;
        ast_node_if_statement_t if_statement;
        ast_node_switch_case_t switch_case;
        ast_node_switch_t switch_statement;
        ast_node_type_ref_t type_ref;
        ast_node_loop_statement_t while_statement;
        ast_node_do_while_statement_t do_while_statement;
        ast_node_for_statement_t for_statement;
        ast_node_goto_statement_t goto_statement;
        ast_node_return_statement_t return_statement;
        ast_node_expression_statement_t expression_statement;
        ast_node_init_declarator_t init_declarator;
        ast_node_declarator_t declarator;
        ast_node_typedef_declarator_t typedef_declarator;
        ast_node_typedef_direct_declarator_t typedef_direct_declarator;
        ast_node_identifier_t identifier;
        ast_node_compound_literal_t compound_literal;
        ast_node_postfix_expression_t postfix_expression;
        ast_node_initializer_list_entry_t initializer_list_entry;
        ast_node_struct_definition_t struct_definition;
        ast_node_enum_definition_t enum_definition;
        ast_node_function_pointer_declarator_t function_pointer_declarator;
        ast_node_enumerator_t enumerator;
        ast_node_unary_expression_prefix_t unary_expression_prefix;
        ast_node_cast_expression_t cast_expression;
        ast_node_binary_expression_t binary_expression;
        ast_node_ternary_operation_t ternary_operation;
        ast_node_conditional_expression_t conditional_expression;
        ast_node_operator_t op;
        ast_node_type_name_t type_name;
        storage_class_data_t storage_class;
        storage_class_specifiers_data_t storage_class_specifiers;
        type_qualifier_data_t type_qualifier;
    };
} c_grammar_node_t;

void c_grammar_node_free(void * node, void * user_data);
