#include <stdio.h>

int
main()
{
    int x = 10;

    switch (x)
    {
    case 0:
        printf("x is 0\n");
        break;
    case 10:
        printf("x is 10\n");
        x = 0;
        break;
    default:
        printf("x is neither 0 nor 1\n");
        break;
    }

    return x;
}
