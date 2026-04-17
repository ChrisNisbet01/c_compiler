typedef struct
{
    int value;
} Inner;
typedef struct
{
    Inner * ptr; // Pointer to struct - triggers the bug
} Outer;
int
main(void)
{
    Inner inner = {0};
    Outer o;
    o.ptr = &inner;
    int v = o.ptr->value; // This accesses the member via arrow

    return v;
}
