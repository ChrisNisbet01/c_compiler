static void foo(unsigned int x);

static void
foo(unsigned int x)
{
    (void)x;
}
int
main(void)
{
    foo(1);
    return 0;
}
