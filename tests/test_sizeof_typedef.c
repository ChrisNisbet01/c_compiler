#include <stdio.h>

typedef int MyInt;

int
main()
{
    MyInt x = 10;
    int result = 0;

    if (sizeof(MyInt) != sizeof(int))
        result = 100 + sizeof(MyInt);

    return result;
}