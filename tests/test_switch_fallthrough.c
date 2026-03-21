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
    case 1:
        printf("test a 2 of 3\n");
    case 2:
        printf("test a 3 of 3\n");
        break;
    default:
        printf("test a shouldn't appear\n");
        res++;
        break;
    }
    res--;
    x = 1;
    switch (x)
    {
    case 0:
    case 1:
        printf("test b 1 of 2\n");
    case 2:
        printf("test b 2 of 2\n");
        break;
    default:
        printf("test b shouldn't appear\n");
        res++;
        break;
    }
    res--;
    x = 2;
    switch (x)
    {
    case 0:
    case 1:
    case 2:
        printf("test c 1 of 2\n");
    default:
        printf("test c 2 of 2\n");
        break;
    }
    res--;
    return res;
}
