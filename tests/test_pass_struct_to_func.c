struct s
{
    int i;
};

int
calc(struct s s)
{
    return s.i + 3;
}

int
main()
{
    int i = calc((struct s){0});

    return i - 3;
}
