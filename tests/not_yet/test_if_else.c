#include <stdio.h>

int
main()
{
    int x = 10;
    float y = 5.5f;

    if (x > 5)
    {
        printf("x is greater than 5\n");
    }
    else
    {
        printf("x is not greater than 5\n");
    }

    if (y == 5.5f)
    {
        printf("y is equal to 5.5f\n");
    }
    else
    {
        printf("y is not equal to 5.5f\n");
    }

    // Test if without else
    if (x == 10)
    {
        printf("x is indeed 10\n");
    }
    return 0;
}
