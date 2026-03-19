#include <stdio.h>

int
main()
{
    // Multi-dimensional array declaration and initialization
    int matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};

    // Modifying an element
    matrix[0][2] = 30;

    // Accessing elements
    int x = matrix[0][2];
    int y = matrix[1][1];

    return x + y - 35;
}
