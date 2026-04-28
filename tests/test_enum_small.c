enum Number
{
    ZERO = 0,
    ONE = 1,
} Number;

int
main()
{
    enum Number n = ONE;
    if (n != 1)
        return 1;

    return 0;
}
