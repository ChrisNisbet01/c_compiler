#include <stdio.h>

typedef union
{
    int i;
    float f2;
} FloatMember;

int
main()
{
    FloatMember fm;

    fm.i = 42;
    if (fm.i != 42)
    {
        return 1;
    }

    fm.f2 = 42.f;
    if (fm.f2 != 42.f)
    {
        return 2;
    }

    return 0;
}
