#include <stdio.h>

struct Point
{
    int x;
    int y;
};

int
main()
{
    struct Point p = {1, 2};

    if (p.x != 1)
    {
        return 1;
    }

    if (p.y != 2)
    {
        return 2;
    }

    return 0;
}
