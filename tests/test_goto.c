#include <stdio.h>

int
main()
{
    int x = 0;

    goto done;

    x = 1;

done:
    return x;
}
