#include <stdio.h>

int
static_storage_func(void)
{
    static int my_static = 0;

    my_static++;
    return my_static;
}

int
main()
{
    int i;
    int sum = 0;
    for (i = 0; i < 10; i++)
    {
        int val = static_storage_func();
        sum += val;
        printf("it: %u, val: %u, sum: %u\n", i, val, sum);
    }

    return sum - 55;
}
