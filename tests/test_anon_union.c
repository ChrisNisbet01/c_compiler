/* Test anonymous union inside a struct */
typedef struct
{
    int type;
    union
    {
        int int_val;
        float float_val;
    };
    char * text;
} my_node_t;

int
main(void)
{
    my_node_t n;
    n.type = 1;
    n.int_val = 42;
    n.text = "hello";
    return n.int_val - 42;
}
