#include <stdio.h>

int
main()
{
    // Basic array declaration and initialization
    int arr[5] = {1, 2, 3, 4, 5};

    // Accessing elements
    printf("arr[0]: %d\n", arr[0]);
    printf("arr[2]: %d\n", arr[2]);
    printf("arr[4]: %d\n", arr[4]);

    // Modifying an element
    arr[1] = 20;
    printf("arr[1] after modification: %d\n", arr[1]);

    // Array size
    printf("Size of arr (in bytes): %zu\n", sizeof(arr));

    return 0;
}
