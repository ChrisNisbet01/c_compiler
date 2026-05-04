struct point {
    int x;
    int y;
};

int main()
{
    const struct point p;
    p.x = 10;
    return 0;
}