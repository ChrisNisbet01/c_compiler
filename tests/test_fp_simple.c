#include <stdio.h>

int add(int a, int b)
{
    return a + b;
}

int main()
{
    /* Simple function pointer variable */
    int (*operation)(int, int);

    operation = add;

    return 0;
}
