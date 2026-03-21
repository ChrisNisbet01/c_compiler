#include <stdio.h>

int
main()
{
    int x;
    int res = 2;

    x = 0;
    switch (x)
    {
    case 0:
    case 1:
        printf("test d 1 of 2\n");
        res--;
    case 2:
        printf("test d 2 of 2\n");
        res--;
        break;
    default:
        printf("test d shouldn't appear\n");
        res++;
        break;
    }
    return res;
}
