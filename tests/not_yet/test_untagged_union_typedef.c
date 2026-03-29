
/* Test: Untagged union via typedef */
typedef union
{
    int i;
    double d;
} Value;

int
main()
{
    Value v;
    v.i = 42;
    return v.i;
}
