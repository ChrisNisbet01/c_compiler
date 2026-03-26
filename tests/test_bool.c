#include <stdbool.h>

int
main()
{
    _Bool a = true;
    _Bool b = false;

    if (a && b)
    {
        return 1;
    }

    if (!a && !b)
    {
        return 2;
    }

    bool c = true;
    bool d = false;

    if (c && d)
    {
        return 1;
    }

    if (!c && !d)
    {
        return 2;
    }

    return 0;
}
