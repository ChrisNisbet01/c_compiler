#include <stdio.h>

int main() {
    int x = 1;
    
    // Single shift left
    int a = x << 2;
    printf("1 << 2 = %d\n", a);
    
    // Single shift right
    int b = 16 >> 2;
    printf("16 >> 2 = %d\n", b);
    
    // Chained shifts: (8 << 1) << 2 = 8 * 2 * 4 = 64
    int c = 8 << 1 << 2;
    printf("8 << 1 << 2 = %d\n", c);
    
    // More chained shifts
    int d = 2 << 3 << 1;
    printf("2 << 3 << 1 = %d\n", d);
    
    return 0;
}
