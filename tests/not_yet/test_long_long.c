#include <stdio.h>

int
main()
{
    long long x = 10000000000LL;
    printf("x: %lld\n", x);
    if (x != 10000000000LL) return 1;
    
    long long y = x * 2;
    printf("y: %lld\n", y);
    if (y != 20000000000LL) return 2;
    
    return 0;
}
