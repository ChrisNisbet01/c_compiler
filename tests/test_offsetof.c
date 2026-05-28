#include <stddef.h>

struct S2
{
    char c;
    int i;
};

struct S
{
    int member1;
    int member2;
    struct S2 s2;
};

int
main()
{
    struct S s;

    if (offsetof(struct S, member1) != 0)
    {
        return 1;
    }

    if (offsetof(struct S, member2) != 4)
    {
        return 2;
    }

    if (offsetof(struct S, s2.c) != 8)
    {
        return 3;
    }

    return 0;
}
