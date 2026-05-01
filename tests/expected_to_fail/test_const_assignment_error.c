#include <stdio.h>

int
main()
{
    int const xxxx = 5;
    xxxx = 10;
    printf("xxxx = %d\n", xxxx);
    return 0;
}