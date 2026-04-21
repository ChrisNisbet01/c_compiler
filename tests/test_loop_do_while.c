#include <stdio.h>

int
main()
{
    int count = 0;
    int sum_while = 0;
    do
    {
        sum_while += count;
        count++;
    } while (count < 5);
    printf("Sum do while loop (0-4): %d\n", sum_while); // Expected: 10

    return sum_while - 10;
}
