#include <stdio.h>

int
main()
{
    int x;
    int res = 2;

    x = 2;
    switch (x)
    {
    case 0:
    case 1:
    case 2:
        printf("test c 1 of 2\n");
        res--;
    default:
        printf("test c 2 of 2\n");
        res--;
        break;
    }

    return res;
}
