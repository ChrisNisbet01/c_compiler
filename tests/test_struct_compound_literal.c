struct Pos
{
    int x;
    int y;
};

int
add_pos(struct Pos p)
{
    return p.x + p.y;
}

int
main()
{
    int r = add_pos((struct Pos){.x = 1, .y = 2});

    return r - 3;
}
