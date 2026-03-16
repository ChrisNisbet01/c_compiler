#include <stdio.h>

int main()
{
    int a = 5;
    int b = 10;
    int c = 5;

    // Relational expressions
    if (a > b)
    {
        printf("a <= c is true\n");
    }
    if (b >= c)
    {
        printf("b >= c is true\n");
    }

    // Equality expressions
    if (a == c)
    {
        printf("a == c is true\n");
    }
    if (a != b)
    {
        printf("a != b is true\n");
    }

    // Bitwise operations
    int x = a & b; // 0101 & 1010 = 0000
    int y = a ^ c; // 0101 ^ 0101 = 0000
    int z = a | c; // 0101 | 0101 = 0101

    printf("x: %d, y: %d, z: %d\n", x, y, z);

    return 0;
}
