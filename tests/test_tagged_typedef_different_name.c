
/* Test: Tagged struct with typedef alias (different name) */
typedef struct TaggedStruct
{
    int x;
    int y;
} Point;

int
main()
{
    struct TaggedStruct p1;

    p1.x = 5;
    p1.y = 10;

    Point p;
    p.x = 10;
    p.y = 20;
    return p.x + p.y + p1.x + p1.y - 45;
}
