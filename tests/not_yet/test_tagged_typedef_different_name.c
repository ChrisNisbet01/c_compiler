
/* Test: Tagged struct with typedef alias (different name) */
typedef struct TaggedStruct
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
