struct Pos
{
    int x : 3;
    int y : 5;
    int z;
};

int
main()
{
    struct Pos p;

    p.x = 1;
    p.y = 7;

    return p.x + p.y - 8;
}
