#include <stdio.h>

struct struct_with_anonymous_union
{
    int member1;
    union
    {
        void * member2;
        void * member3;
    };
    int member4;
};

int
main()
{
    int error = 0;
    unsigned long offset_to_member2 = ((unsigned long)&((struct struct_with_anonymous_union *)0)->member2);
    printf("test: member2 offset is: %lu\n", offset_to_member2);
    if (offset_to_member2 != 8)
    {
        error += 1;
    }
    unsigned long offset_to_member3 = ((unsigned long)&((struct struct_with_anonymous_union *)0)->member3);
    printf("test: member3 offset is: %lu\n", offset_to_member3);
    if (offset_to_member3 != 8)
    {
        error += 2;
    }

    struct struct_with_anonymous_union s = {0};
    void * p2 = &s.member2;
    void * p3 = &s.member3;

    unsigned long p2_offset = (unsigned long)p2 - (unsigned long)&s;
    unsigned long p3_offset = (unsigned long)p3 - (unsigned long)&s;

    printf("p2 offset: %lu\n", p2_offset);
    printf("p3 offset: %lu\n", p3_offset);

    if (p2_offset != 8)
    {
        error += 4;
    }
    if (p3_offset != 8)
    {
        error += 8;
    }

    unsigned long offset_to_member4 = ((unsigned long)&((struct struct_with_anonymous_union *)0)->member4);
    printf("test: member4 offset is: %lu\n", offset_to_member4);
    if (offset_to_member4 != 16)
    {
        error += 16;
    }

    return error;
}
