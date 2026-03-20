struct Point
{
    int x;
    int y;
};

int
main()
{
    struct Point p;
    struct Point * pp = &p;

    pp->x = 10;
    pp->y = 20;

    return p.x + p.y - 30; // 0
}
