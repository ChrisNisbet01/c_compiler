#include <stdarg.h>
#include <stdio.h>

int
sum_numbers(int count, ...)
{
    int sum = 0;
    printf("count: %d\n", count);
    va_list args;

    va_start(args, count);
    for (int i = 0; i < count; i++)
    {
        sum += va_arg(args, int);
    }
    va_end(args);

    return sum;
}

int
main()
{
    int sum = sum_numbers(3, 1, 2, 3);
    printf("sum: %d\n", sum);
    return sum - 6;
}
