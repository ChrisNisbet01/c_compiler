int
main()
{
    int a = __alignof__(long);
    int s = sizeof(long);

    if (a != 4)
    {
        return 1;
    }
    if (s != 8)
    {
        return 2;
    }

    return 0;
}
