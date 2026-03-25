#include <stdio.h>

int
main()
{
    int sum = 0;
    for (int i = 0; i < 5; i++)
    {
        if (i >= 3)
        {
            continue;
        }
        sum += i;
    }
    printf("Sum: %d\n", sum);
    return sum - 3;
}
