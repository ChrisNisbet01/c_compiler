#include <stdio.h>

int
main()
{
    int arr[5] = {10, 20, 30, 40, 50};
    
    int i = 2;
    int val = arr[i];
    printf("arr[%d]: %d\n", i, val);
    if (val != 30) return 1;
    
    i = 0;
    val = arr[i];
    printf("arr[%d]: %d\n", i, val);
    if (val != 10) return 2;
    
    i = 4;
    val = arr[i];
    printf("arr[%d]: %d\n", i, val);
    if (val != 50) return 3;
    
    return 0;
}
