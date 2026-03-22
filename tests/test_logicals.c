int
main()
{
    int x = 2;
    int a = 5;
    int b = 4;
    int c = 0;

    if (a && b)
    {
        x--;
    }
    if (a && c)
    {
        x--;
    }
    if (c || a)
    {
        x--;
    }
    if (c || c)
    {
        x--;
    }

    return x;
}