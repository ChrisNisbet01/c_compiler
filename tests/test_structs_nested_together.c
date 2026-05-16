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

typedef struct ast_node_float_literal_t
{
    float_literal_data_t float_literal;
} ast_node_float_literal_t;

int
main()
{
    ast_node_float_literal_t s = {0};

    int error = 0;
    unsigned long offset1;

    offset1 = ((unsigned long)&((struct ast_node_float_literal_t *)0)->float_literal);
    printf("test: offset1 is: %lu\n", offset1);
    if (offset1 != 0)
    {
        error |= 1;
    }

    unsigned long size1;

    size1 = sizeof(s.float_literal);

    return error;
}