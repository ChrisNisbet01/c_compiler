#include <stdio.h>

int
main()
{
    int x;
    int arr[10];
    
    int size_int = sizeof(int);
    printf("sizeof(int): %d\n", size_int);
    
    int size_x = sizeof(x);
    printf("sizeof(x): %d\n", size_x);
    
    int size_arr = sizeof(arr);
    printf("sizeof(arr): %d\n", size_arr);
    
    if (size_int != 4) return 1;
    if (size_x != 4) return 2;
    if (size_arr != 40) return 3;
    
    return 0;
}
