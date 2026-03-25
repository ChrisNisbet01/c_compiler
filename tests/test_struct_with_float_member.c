#include <stdio.h>

typedef struct
{
    float f;
    float f2;
} FloatMemberStruct;

int
main()
{
    FloatMemberStruct fm;
    fm.f = 42.f;
    if (fm.f != 42.f)
    {
        return 1;
    }

    return 0;
}
