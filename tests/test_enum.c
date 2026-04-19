typedef enum
{
    ZERO = 0,
    ONE = 1,
    TWO = 2,
    TEN = 10,
} Number;

int
main()
{
    Number n = ONE;
    if (n != 1)
        return 1;

    n = TEN;
    if (n != 10)
        return 2;

    return 0;
}
