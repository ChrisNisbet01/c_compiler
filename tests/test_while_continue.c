#include <stdio.h>

int
main()
{
    int i = 0;
    int sum = 0;
    while (i < 5)
    {
        i++;
        if (i == 3)
        {
            continue;
        }
        sum += i;
    }
    printf("Sum: %d\n", sum);
    return sum - 12;
}
