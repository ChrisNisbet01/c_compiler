#include <stdio.h>

int
main()
{
    int x = (1, 2, 3);
    printf("x: %d\n", x);
    if (x != 3) return 1;
    
    int a = 10;
    int b = (a = 5, a + 3);
    printf("b: %d\n", b);
    if (b != 8) return 2;
    
    return 0;
}
