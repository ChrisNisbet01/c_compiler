
/* Test: Typedef referring to tagged struct (forward declaration) */
typedef struct Point
{
    int x;
    int y;
} PointType;

typedef struct Point2 Point2Type;

int
main()
{
    struct Point p;
    return 0;
}
