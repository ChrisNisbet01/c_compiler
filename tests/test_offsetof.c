#include <stddef.h>

struct S
{
    int member1;
    int member2;
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

    return 0;
}
