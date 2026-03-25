#include <stdio.h>

enum Number
{
    ZERO = 0,
    ONE = 1,
    TWO = 2,
    TEN = 10
} Number;

int
main()
{
    enum Number n = ONE;
    printf("n: %d\n", n);
    if (n != 1)
        return 1;

    n = TEN;
    printf("n: %d\n", n);
    if (n != 10)
        return 2;

    return 0;
}
