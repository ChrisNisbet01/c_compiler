#include <stdio.h>

int
main()
{
    int i = 0;
    int sum = 0;
    while (i < 10)
    {
        if (i == 5)
        {
            break;
        }
        sum += i;
        i++;
    }
    printf("Sum: %d\n", sum);
    return sum - 10;
}
