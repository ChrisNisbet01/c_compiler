#include <stdbool.h>

int
main()
{
    _Bool a = true;
    _Bool b = false;

    if (!a && !b)
    {
        return 2;
    }

    return 0;
}
