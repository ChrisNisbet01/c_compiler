struct Point
{
    int x;
    int y;
};

int
main()
{
    struct Point p = {.x = 10, .y = 20};
    struct Point * p_ptr = &p;

    return p_ptr.x + p_ptr.y - 30;
}
