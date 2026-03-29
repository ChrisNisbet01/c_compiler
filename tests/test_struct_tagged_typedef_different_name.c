
/* Test: Tagged struct where struct tag differs from typedef name */
struct Point
{
    int x;
    int y;
};

typedef struct Point Coord;

int
main()
{
    struct Point p;
    p.x = 10;
    p.y = 20;

    Coord c;
    c.x = 5;
    c.y = 15;

    return p.x + p.y + c.x + c.y - 50;
}
