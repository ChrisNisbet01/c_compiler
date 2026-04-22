/* Test arrow member access on anonymous struct typedefs (typedef struct { } name_t) */
typedef struct
{
    int x;
    int y;
} point_t;

typedef struct
{
    point_t * origin;
    int count;
} collection_t;

int
main(void)
{
    point_t p;
    p.x = 3;
    p.y = 4;

    collection_t c;
    c.origin = &p;
    c.count = 1;

    collection_t * cp = &c;

    /* Arrow access on anonymous struct typedef */
    int result = cp->origin->x + cp->origin->y + cp->count;

    return result - 8; /* 3 + 4 + 1 = 8, return 0 on success */
}
