// Minimal test case that should trigger arrow access
typedef struct
{
    int value;
} Inner;

typedef struct
{
    Inner * inner; // Pointer to struct - like scope->symbols
} Outer;

int
main(void)
{
    Inner inner = {0};
    Outer o;
    o.inner = &inner;
    return o.inner->value; // Arrow access
}