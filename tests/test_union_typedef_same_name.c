
/* Test: Tagged union with typedef alias (same name) */
typedef union Value
{
    int i;
    double d;
} Value;

int
main()
{
    Value v;
    v.i = 20;
    return v.i - 20;
}
