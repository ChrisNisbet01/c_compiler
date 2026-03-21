#include <stdio.h>

int
main()
{
    int x = 0;
    int res = 3;

    switch (x)
    {
    case 0:
        printf("test a 1 of 3\n");
        res--;
    case 1:
        printf("test a 2 of 3\n");
        res--;
    case 2:
        printf("test a 3 of 3\n");
        res--;
        break;
    default:
        printf("test a shouldn't appear\n");
        res++;
        break;
    }

    return res;
}
