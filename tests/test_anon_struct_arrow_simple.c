/* Test arrow member access on anonymous struct typedefs (typedef struct { } name_t) */
typedef struct
{
    int x;
    int y;
} point_t;

int
main(void)
{
    point_t p;
    point_t * pp = &p;

    p.x = 3;
    p.y = 4;

    int result = pp->x + pp->y;

    return result - 7; /* 3 + 4  = 7 - return 0 on success */
}
