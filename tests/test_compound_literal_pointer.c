struct Point
{
    int x;
    int y;
};

int
get_x(struct Point *p)
{
    return p->x;
}

int
main()
{
    struct Point *p = &((struct Point){1, 2});

    if (get_x(p) != 1)
    {
        return 1;
    }

    return 0;
}
