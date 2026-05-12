struct s
{
    int i1;
    int i2;
    int i3;
    int i4;
    int i5;
    int i6;
};

int
calc(struct s s)
{
    return s.i1 + s.i2 + s.i3 + s.i4 + s.i5 + s.i6;
}

int
main()
{
    struct s v = {1, 2, 3, 4, 5, 6};
    int i = calc(v);

    return i - 21;
}
