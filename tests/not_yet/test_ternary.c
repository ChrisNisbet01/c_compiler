#include <stdio.h>

int
main()
{
    int a = 10;
    int b = 20;
    
    int max = (a > b) ? a : b;
    printf("max: %d\n", max);
    if (max != 20) return 1;
    
    int min = (a < b) ? a : b;
    printf("min: %d\n", min);
    if (min != 10) return 2;
    
    return 0;
}
