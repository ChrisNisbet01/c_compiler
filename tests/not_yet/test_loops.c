#include <stdio.h>

int
main()
{
    // For loop
    int sum_for = 0;
    for (int i = 0; i < 5; i++)
    { // i goes from 0 to 4
        sum_for += i;
    }
    printf("Sum for loop (0-4): %d\n", sum_for); // Expected: 10

    // While loop
    int count = 0;
    int sum_while = 0;
    while (count < 5)
    {
        sum_while += count;
        count++;
    }
    printf("Sum while loop (0-4): %d\n", sum_while); // Expected: 10

    return 0;
}
