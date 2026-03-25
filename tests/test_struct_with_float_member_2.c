#include <stdio.h>

typedef struct
{
    float f;
    float f2;
} FloatMember;

int
main()
{
    FloatMember fm;
    fm.f2 = 42.f;
    if (fm.f2 != 42.f)
    {
        return 1;
    }

    return 0;
}
