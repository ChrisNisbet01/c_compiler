struct Point
{
    int x;
    int y;
};

int
main()
{
    int cond = 1;

    struct Point p = cond ? (struct Point){1, 2} : (struct Point){3, 4};

    if (p.x != 1)
    {
        return 1;
    }

    if (p.y != 2)
    {
        return 2;
    }

    cond = 0;
    struct Point q = cond ? (struct Point){1, 2} : (struct Point){3, 4};

    if (q.x != 3)
    {
        return 3;
    }

    if (q.y != 4)
    {
        return 4;
    }

    return 0;
}
