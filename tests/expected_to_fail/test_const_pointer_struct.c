struct point
{
    int x;
    int y;
};

int
main()
{
    struct point pnt = {1, 2};
    struct point const * cp = &pnt;
    struct point * cp2 = &pnt;

    cp2->x = 3;
    cp->x = 4; /* Should generate a compiler error. */

    printf("cp->x: %d, cp2->x: %d\n", cp->x, cp2->x);
    return cp->x - 3; /* Proves that the assignment through cp didn't take place. */
}
