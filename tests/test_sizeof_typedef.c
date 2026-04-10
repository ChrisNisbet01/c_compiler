#include <stdio.h>

typedef int MyInt;

int
main()
{
    MyInt x = 10;
    int result = 0;

    if (sizeof(MyInt) != sizeof(int))
        result = 1;

    if (sizeof(x) != sizeof(int))
        result = 2;

    return result;
}