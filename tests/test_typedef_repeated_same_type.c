typedef struct point point_t;
typedef struct point point_t2;

typedef struct point
{
    int x;
    int y;
} point_t3;

int
main()
{
    point_t p1;
    point_t2 p2;
    point_t3 p3;
    p1.x = 1;
    p2.y = 2;
    p3.x = p1.x + p2.y;
    return p3.x - 3;
}