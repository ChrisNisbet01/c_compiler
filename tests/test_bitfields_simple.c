struct Pos
{
    int a : 3;
    int b : 5;
    int c : 8;
    int z;
};

int
main()
{
    struct Pos p = {0};

    p.b = 6;

    return p.b + sizeof(p) - 14;
}
