#include <stdio.h>

typedef union
{
    int i;
} IntInt;

int
main()
{
    IntInt u;
    u.i = 42;
    if (u.i != 42)
    {
        return 1;
    }

    return 0;
}
