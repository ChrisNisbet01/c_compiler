#include <stdio.h>

volatile int global_volatile = 10;

int
main()
{
    printf("global_volatile = %d\n", global_volatile);
    return 0;
}