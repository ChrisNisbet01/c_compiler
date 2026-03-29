
/* Test: Tagged union at file scope */
union Value
{
    int i;
    double d;
};

int
main()
{
    union Value v;
    v.i = 42;
    return v.i - 42;
}
