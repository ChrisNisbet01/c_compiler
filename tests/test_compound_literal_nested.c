struct Inner
{
    int x;
    int y;
};

struct Outer
{
    struct Inner inner;
    int z;
};

int
main()
{
    struct Outer o = (struct Outer){.inner = {.x = 1, .y = 2}, .z = 3};

    if (o.inner.x != 1)
    {
        return 1;
    }

    if (o.inner.y != 2)
    {
        return 2;
    }

    if (o.z != 3)
    {
        return 3;
    }

    return 0;
}
