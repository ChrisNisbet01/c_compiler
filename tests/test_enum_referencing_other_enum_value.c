#include <stdio.h>

typedef enum
{
    FIRST = 0,
    ZERO = 0,
    ONE = 1,
    TWO = 2,
    TEN = 10,
    TENAGAIN = TEN,
} Number;

int
main()
{
    Number n = ONE;
    printf("n: %d\n", n);
    if (n != 1)
        return 1;

    n = TEN;
    printf("n: %d\n", n);
    if (n != 10)
        return 2;

    n = TENAGAIN;
    printf("n: %d\n", n);
    if (n != 10)
        return 3;

    return 0;
}
