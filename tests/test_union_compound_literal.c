union Data
{
    int x;
    int y;
};

int
main()
{
    union Data d = (union Data){.x = 1};

    if (d.x != 1)
    {
        return 1;
    }

    return 0;
}
