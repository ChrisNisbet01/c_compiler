#include <stdio.h>

union standalone_un
{
    int member1;
    int member2;
    int member3;
};

int
main()
{
    unsigned long offset_to_member1 = ((unsigned long)&((union standalone_un *)0)->member1);
    printf("test: member1 offset is: %lu\n", offset_to_member1);
    if (offset_to_member1 != 0)
    {
        return 1;
    }
    unsigned long offset_to_member2 = ((unsigned long)&((union standalone_un *)0)->member2);
    printf("test: member2 offset is: %lu\n", offset_to_member2);
    if (offset_to_member2 != 0)
    {
        return 2;
    }
    unsigned long offset_to_member3 = ((unsigned long)&((union standalone_un *)0)->member3);
    printf("test: member3 offset is: %lu\n", offset_to_member3);
    if (offset_to_member3 != 0)
    {
        return 3;
    }
    return 0;
}
