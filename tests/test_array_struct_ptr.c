
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
    Inner inner[2] = {0};
    inner[0].value = 10;

    // Create
    Outer o;
    o.inner = inner;
    int v = o.inner[0].value;
    printf("v: %d\n", v);
    return v - 10; // array access
}
