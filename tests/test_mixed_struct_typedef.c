#include <stdio.h>

/* Test: Mix of tagged struct and typedef usage */
struct Point
{
    int x;
    int y;
};

typedef struct Point TaggedPoint;

int
main()
{
    /* Use tagged struct */
    struct Point p1;
    p1.x = 10;
    p1.y = 20;

    /* Use typedef */
    TaggedPoint p2;
    p2.x = 5;
    p2.y = 15;

    printf("p1 (%p): (%d, %d)\n", &p1, p1.x, p1.y);
    printf("p2 (%p): (%d, %d)\n", &p2, p2.x, p2.y);

    return p1.x + p1.y + p2.x + p2.y - 50;
}
