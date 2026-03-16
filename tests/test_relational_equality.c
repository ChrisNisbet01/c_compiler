#include <stdio.h>

int main()
{
    int a = 5;
    int b = 10;
    int c = 5;

    // Relational expressions
    if (a < b)
    {
        printf("a < b is true\n");
    }
    if (a > b)
    {
        printf("a > b is true\n");
    }
    if (a <= c)
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

    return 0;
}
