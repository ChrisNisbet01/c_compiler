struct point {
    int x;
    int y;
};

int main()
{
    const struct point p = {1, 2};
    p = p;
    return 0;
}