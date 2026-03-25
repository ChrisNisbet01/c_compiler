#include "ast_node_name.h"

typedef struct
{
    char const * name;
} node_type_name_t;

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static node_type_name_t const node_type_names[] = {
    [AST_NODE_TRANSLATION_UNIT] = {.name = "TranslationUnit"},
    [AST_NODE_FUNCTION_DEFINITION] = {.name = "FunctionDefinition"},
    [AST_NODE_COMPOUND_STATEMENT] = {.name = "CompoundStatement"},
    [AST_NODE_DECLARATION] = {.name = "Declaration"},
    [AST_NODE_INTEGER_BASE] = {.name = "IntegerLiteral"},
    [AST_NODE_FLOAT_BASE] = {.name = "FloatLiteral"},
    [AST_NODE_INTEGER_LITERAL] = {.name = "IntegerLiteral"},
    [AST_NODE_FLOAT_LITERAL] = {.name = "FloatLiteral"},
    [AST_NODE_STRING_LITERAL] = {.name = "StringLiteral"},
    [AST_NODE_LITERAL_SUFFIX] = {.name = "LiteralSuffix"},
    [AST_NODE_IDENTIFIER] = {.name = "Identifier"},
    [AST_NODE_DECL_SPECIFIERS] = {.name = "DeclarationSpecifiers"},
    [AST_NODE_ASSIGNMENT] = {.name = "Assignment"},
    [AST_NODE_TYPE_SPECIFIER] = {.name = "TypeSpecifier"},
    [AST_NODE_UNARY_OPERATOR] = {.name = "UnaryOperator"},
    [AST_NODE_UNARY_EXPRESSION] = {.name = "UnaryExpression"},
    [AST_NODE_DECLARATOR] = {.name = "Declarator"},
    [AST_NODE_DIRECT_DECLARATOR] = {.name = "DirectDeclarator"},
    [AST_NODE_DECLARATOR_SUFFIX] = {.name = "DeclaratorSuffix"},
    [AST_NODE_POINTER] = {.name = "Pointer"},
    [AST_NODE_RELATIONAL_OPERATOR] = {.name = "RelationalOperator"},
    [AST_NODE_RELATIONAL_EXPRESSION] = {.name = "RelationalExpression"},
    [AST_NODE_EQUALITY_OPERATOR] = {.name = "EqualityOperator"},
    [AST_NODE_EQUALITY_EXPRESSION] = {.name = "EqualityExpression"},
    [AST_NODE_BITWISE_EXPRESSION] = {.name = "BitwiseExpression"},
    [AST_NODE_LOGICAL_EXPRESSION] = {.name = "LogicalExpression"},
    [AST_NODE_SHIFT_OPERATOR] = {.name = "ShiftOperator"},
    [AST_NODE_SHIFT_EXPRESSION] = {.name = "ShiftExpression"},
    [AST_NODE_ARITHMETIC_OPERATOR] = {.name = "ArithmeticOperator"},
    [AST_NODE_ARITHMETIC_EXPRESSION] = {.name = "ArithmeticExpression"},
    [AST_NODE_FUNCTION_CALL] = {.name = "FunctionCall"},
    [AST_NODE_POSTFIX_PARTS] = {.name = "PostfixParts"},
    [AST_NODE_POSTFIX_EXPRESSION] = {.name = "PostfixExpression"},
    [AST_NODE_POSTFIX_OPERATOR] = {.name = "PostfixOperator"},
    [AST_NODE_ARRAY_SUBSCRIPT] = {.name = "ArraySubscript"},
    [AST_NODE_MEMBER_ACCESS_DOT] = {.name = "MemberAccessDot"},
    [AST_NODE_MEMBER_ACCESS_ARROW] = {.name = "MemberAccessArrow"},
    [AST_NODE_CAST_EXPRESSION] = {.name = "CastExpression"},
    [AST_NODE_INIT_DECLARATOR] = {.name = "InitDeclarator"},
    [AST_NODE_IF_STATEMENT] = {.name = "IfStatement"},
    [AST_NODE_SWITCH_STATEMENT] = {.name = "SwitchStatement"},
    [AST_NODE_WHILE_STATEMENT] = {.name = "WhileStatement"},
    [AST_NODE_DO_WHILE_STATEMENT] = {.name = "DoWhileStatement"},
    [AST_NODE_FOR_STATEMENT] = {.name = "ForStatement"},
    [AST_NODE_GOTO_STATEMENT] = {.name = "GotoStatement"},
    [AST_NODE_CONTINUE_STATEMENT] = {.name = "ContinueStatement"},
    [AST_NODE_BREAK_STATEMENT] = {.name = "BreakStatement"},
    [AST_NODE_RETURN_STATEMENT] = {.name = "ReturnStatement"},
    [AST_NODE_TYPE_NAME] = {.name = "TypeName"},
    [AST_NODE_EXPRESSION_STATEMENT] = {.name = "ExpressionStatement"},
    [AST_NODE_STRUCT_DEFINITION] = {.name = "StructDefinition"},
    [AST_NODE_TYPEDEF_DECLARATION] = {.name = "TypedefDeclaration"},
    [AST_NODE_INITIALIZER_LIST] = {.name = "InitializerList"},
    [AST_NODE_LABELED_STATEMENT] = {.name = "LabeledStatement"},
    [AST_NODE_CHARACTER_LITERAL] = {.name = "CharacterLiteral"},
    [AST_NODE_CASE_LABEL] = {.name = "CaseLabel"},
    [AST_NODE_SWITCH_CASE] = {.name = "SwitchCase"},
    [AST_NODE_DEFAULT_STATEMENT] = {.name = "DefaultStatement"},
    [AST_NODE_LABELED_IDENTIFIER] = {.name = "LabeledIdentifier"},
    [AST_NODE_ASSIGNMENT_OPERATOR] = {.name = "AssignmentOperator"},
    [AST_NODE_OPTIONAL_KW_EXTENSION] = {.name = "OptionalKwExtension"},
    [AST_NODE_OPTIONAL_INIT_DECLARATOR_LIST] = {.name = "OptionalInitDeclaratorList"},
    [AST_NODE_KEYWORD] = {.name = "Keyword"},
};

#define NUM_NODE_TYPE_NAMES ARRAY_SIZE(node_type_names)

char const *
get_node_type_name_from_type(c_grammar_node_type_t node_type)
{
    if (node_type < 0 || node_type >= NUM_NODE_TYPE_NAMES || node_type_names[node_type].name == NULL)
    {
        return "Unknown";
    }

    return node_type_names[node_type].name;
}

char const *
get_node_type_name_from_node(c_grammar_node_t const * node)
{
    if (node == NULL)
    {
        return "NULL";
    }
    return get_node_type_name_from_type(node->type);
}
