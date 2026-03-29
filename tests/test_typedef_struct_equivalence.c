
/* Test: Verify typedef struct is equivalent to struct tag */
struct Point
{
    int x;
    int y;
};

typedef struct Point PointType;

int
main()
{
    /* Declare using struct tag */
    struct Point p1;
    p1.x = 10;
    p1.y = 20;

    /* Declare using typedef */
    PointType p2;
    p2.x = 30;
    p2.y = 40;

    /* Access through both should work identically */
    int sum1 = p1.x + p1.y;
    int sum2 = p2.x + p2.y;

    return (sum1 - 30) + (sum2 - 70);
}
