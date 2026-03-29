
/* Test: Tagged union with typedef alias (different name) */
typedef union TaggedUnion
{
    int i;
    double d;
} Value;

int
main()
{
    Value v;
    v.i = 10;
    return v.i - 10;
}
