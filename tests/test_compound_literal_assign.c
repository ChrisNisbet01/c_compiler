struct Point
{
    int x;
    int y;
};

int
main()
{
    struct Point p = (struct Point){.x = 1, .y = 2};

    if (p.x != 1)
    {
        return 1;
    }

    if (p.y != 2)
    {
        return 2;
    }

    return 0;
}
