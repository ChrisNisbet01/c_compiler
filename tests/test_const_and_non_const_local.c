#include <stdio.h>

int
main()
{
    int const local_const = 42;
    int local = 42;
    local++;
    printf("local_const = %d\n", local_const);
    return 0;
}