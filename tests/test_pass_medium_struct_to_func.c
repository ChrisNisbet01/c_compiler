struct s
{
    int i1;
    int i2;
    int i3;
};

int
calc(struct s s)
{
    return s.i1 + s.i2 + s.i3;
}

int
main()
{
    struct s v = {1, 2, 3};
    int i = calc(v);

    return i - 6;
}
