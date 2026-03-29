
/* Test: Tagged struct with typedef alias (same name) */
typedef struct Point
{
    int x;
    int y;
} Point;

int
main()
{
    Point p;
    p.x = 10;
    p.y = 20;
    return p.x + p.y;
}
