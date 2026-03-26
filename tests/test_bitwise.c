#include <stdbool.h>

int
main()
{
    _Bool a = true;
    _Bool b = false;

    return (a ^ b) & 2;
}
