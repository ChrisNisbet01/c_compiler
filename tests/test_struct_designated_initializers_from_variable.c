#include <stdio.h>

struct Point
{
    int x;
    int y;
};

int
main()
{
    int a = 1;
    struct Point p = {.x = a, .y = a};

    if (p.x != 1)
    {
        return 1;
    }

    if (p.y != 1)
    {
        return 2;
    }

    return 0;
}
