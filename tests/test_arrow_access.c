typedef struct
{
    int value;
} Inner;
typedef struct
{
    Inner * inr;
} Outer;
typedef struct A
{
    int dummy;
} A_t;

int
main(void)
{
    Inner inner = {0};
    Outer o;
    o.inr = &inner;
    int v = o.inr->value;

    return v;
}
