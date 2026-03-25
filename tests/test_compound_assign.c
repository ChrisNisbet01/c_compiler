#include <stdio.h>

int
main()
{
    int x = 10;
    x += 5;
    printf("x after +=: %d\n", x);
    if (x != 15) return 1;
    
    x -= 3;
    printf("x after -=: %d\n", x);
    if (x != 12) return 2;
    
    x *= 2;
    printf("x after *=: %d\n", x);
    if (x != 24) return 3;
    
    x /= 4;
    printf("x after /=: %d\n", x);
    if (x != 6) return 4;
    
    return 0;
}
