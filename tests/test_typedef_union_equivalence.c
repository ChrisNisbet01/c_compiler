
/* Test: Verify typedef union is equivalent to union tag */
union Value
{
    int i;
    double d;
};

typedef union Value ValueType;

int
main()
{
    /* Declare using union tag */
    union Value v1;
    v1.i = 42;

    /* Declare using typedef */
    ValueType v2;
    v2.i = 58;

    /* Access through both should work identically */
    return v1.i + v2.i - 100;
}
