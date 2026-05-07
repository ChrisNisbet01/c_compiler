int
main(void)
{
    int i = 0;
    int j = 1;
    int * const pi = &i;

    pi = &j; /* Should fail to compile. */

    return 0;
}
