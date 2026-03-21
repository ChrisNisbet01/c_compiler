#include <stdio.h>

int
main()
{
    int x;
    int res = 2;

    x = 1;
    switch (x)
    {
    case 0:
    case 1:
        printf("test b 1 of 2\n");
        res--;
    case 2:
        printf("test b 2 of 2\n");
        res--;
        break;
    default:
        printf("test b shouldn't appear\n");
        res++;
        break;
    }
    return res;
}
