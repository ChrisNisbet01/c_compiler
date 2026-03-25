#include <stdio.h>

struct Point
{
    int x;
    int y;
};

int
main()
{
    struct Point p = {0};

    if (p.x != 0)
    {
        return 1;
    }

    if (p.y != 0)
    {
        return 2;
    }

    return 0;
}
