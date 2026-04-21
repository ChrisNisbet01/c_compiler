#include <stdio.h>

int
main()
{
    // While loop
    int count = 0;
    int sum_while = 0;
    while (count < 5)
    {
        sum_while += count;
        count++;
    }
    printf("Sum while loop (0-4): %d\n", sum_while); // Expected: 10

    return sum_while - 10;
}
