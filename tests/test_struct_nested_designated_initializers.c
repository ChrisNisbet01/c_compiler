struct Pos
{
    int x;
    int y;
};

struct Point
{
    struct Pos pos;
};

int
main()
{
    int a = 1;
    struct Point p = {.pos.x = a, .pos.y = a};

    if (p.pos.x != 1)
    {
        return 1;
    }

    if (p.pos.y != 1)
    {
        return 2;
    }

    return 0;
}
