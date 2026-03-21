#include <stdio.h>

int
main()
{
    int x = 10;

    switch (x)
    {
    case 10:
        printf("x is 10\n");
        x = 0;
        break;
    case 1:
        printf("x is 1\n");
        break;
    default:
        printf("x is neither 0 nor 1\n");
        break;
    }

    return x;
}
