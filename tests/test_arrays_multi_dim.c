#include <stdio.h>

int
main()
{
    // Multi-dimensional array declaration and initialization
    int matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};

    // Accessing elements
    printf("matrix[0][0]: %d\n", matrix[0][0]);
    printf("matrix[0][1]: %d\n", matrix[0][1]);
    printf("matrix[1][0]: %d\n", matrix[1][0]);
    printf("matrix[1][2]: %d\n", matrix[1][2]);

    // Modifying an element
    matrix[0][2] = 30;
    printf("matrix[0][2] after modification: %d\n", matrix[0][2]);

    // Array size
    printf("Size of matrix (in bytes): %zu\n", sizeof(matrix));
    printf("Size of matrix[0] (in bytes): %zu\n", sizeof(matrix[0]));

    return 0;
}
