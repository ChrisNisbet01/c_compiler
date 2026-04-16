#include <stdio.h>

unsigned long long
bswap64(unsigned long long x)
{
    return (
        (((x) & 0xff00000000000000ull) >> 56) | (((x) & 0x00ff000000000000ull) >> 40)
        | (((x) & 0x0000ff0000000000ull) >> 24) | (((x) & 0x000000ff00000000ull) >> 8)
        | (((x) & 0x00000000ff000000ull) << 8) | (((x) & 0x0000000000ff0000ull) << 24)
        | (((x) & 0x000000000000ff00ull) << 40) | (((x) & 0x00000000000000ffull) << 56)
    );
}

int
main(void)
{
    unsigned long long x_swapped = bswap64(0x123456789abcdef0ull);
    printf("%llx\n", x_swapped);
    return x_swapped - 0xf0debc9a78563412ull;
}
