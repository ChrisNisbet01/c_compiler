struct Point
{
    int x;
    int y;
};

int
main()
{
    struct Point arr[2];
    arr[0] = (struct Point){1, 2};
    arr[1] = (struct Point){3, 4};

    if (arr[0].x != 1)
    {
        return 1;
    }

    if (arr[0].y != 2)
    {
        return 2;
    }

    if (arr[1].x != 3)
    {
        return 3;
    }

    if (arr[1].y != 4)
    {
        return 4;
    }

    return 0;
}
