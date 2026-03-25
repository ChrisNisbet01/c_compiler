#include <stdio.h>

int
main()
{
    int x = 42;
    int * p = &x;
    int y = *p;
    printf("y: %d\n", y);
    return y - 42;
}
