#include <stdio.h>

typedef int my_int;
typedef my_int another_int;

int
main()
{
    my_int x = 42;
    another_int y = 10;
    printf("x: %d, y: %d\n", x, y);
    if (x != 42) return 1;
    if (y != 10) return 2;
    return 0;
}
