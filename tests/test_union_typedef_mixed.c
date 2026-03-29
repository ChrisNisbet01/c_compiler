
/* Test: Mix of tagged union and typedef usage */
union Value
{
    int i;
    double d;
};

typedef union Value TaggedValue;

int
main()
{
    /* Use tagged union */
    union Value v1;
    v1.i = 10;

    /* Use typedef */
    TaggedValue v2;
    v2.i = 5;

    return v1.i + v2.i - 15;
}
