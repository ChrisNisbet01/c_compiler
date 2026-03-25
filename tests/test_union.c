#include <stdio.h>

typedef union
{
    int i;
    float f;
} IntFloat;

int
main()
{
    IntFloat u;
    u.i = 42;
    printf("u as int: %d\n", u.i);
    if (u.i != 42) return 1;
    
    u.f = 3.14f;
    printf("u as float: %f\n", u.f);
    
    return 0;
}
