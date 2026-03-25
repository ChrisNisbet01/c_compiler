#include <stdio.h>

int
main()
{
    int sum = 0;
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            if (j == 1)
            {
                break;
            }
            sum += 1;
        }
    }
    printf("Sum: %d\n", sum);
    return sum - 3;
}
