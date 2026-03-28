#include <stdio.h>

struct Pos
{
    int a : 3;
    int b : 5;
    int c : 8;
    int z;
};

int
main()
{
    struct Pos p = {0};

    p.a = 1;
    printf("After a: a: %d, b: %d, c: %d, z: %d, size: %lu\n", p.a, p.b, p.c, p.z, (unsigned long)sizeof(p));

    p.b = 6;
    printf("After b: a: %d, b: %d, c: %d, z: %d, size: %lu\n", p.a, p.b, p.c, p.z, (unsigned long)sizeof(p));

    p.c = 200;
    printf("After c: a: %d, b: %d, c: %d, z: %d, size: %lu\n", p.a, p.b, p.c, p.z, (unsigned long)sizeof(p));

    p.z = 42;
    printf("After z: a: %d, b: %d, c: %d, z: %d, size: %lu\n", p.a, p.b, p.c, p.z, (unsigned long)sizeof(p));

    return p.a + p.b + p.c + p.z + sizeof(p) - 257;
}
