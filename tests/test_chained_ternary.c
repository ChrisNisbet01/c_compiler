#include <stdio.h>

int main() {
    int a = 1;
    
    // Test 1: (a < 0) ? 10 : (a == 1 ? 100 : 200)
    // a=1: a<0 is false, so we evaluate (a==1 ? 100 : 200), which is true, so 100
    int result1 = (a < 0) ? 10 : (a == 1 ? 100 : 200);
    printf("test1: %d (expected 100)\n", result1);
    if (result1 != 100) return 1;
    
    // Test 2: (a < 0) ? 10 : (a == 2 ? 100 : 200)  
    // a=1: a<0 is false, so we evaluate (a==2 ? 100 : 200), which is false, so 200
    int result2 = (a < 0) ? 10 : (a == 2 ? 100 : 200);
    printf("test2: %d (expected 200)\n", result2);
    if (result2 != 200) return 2;
    
    // Test 3: (a > 0) ? (a == 1 ? 100 : 200) : 300
    // a=1: a>0 is true, so we evaluate (a==1 ? 100 : 200), which is true, so 100
    int result3 = (a > 0) ? (a == 1 ? 100 : 200) : 300;
    printf("test3: %d (expected 100)\n", result3);
    if (result3 != 100) return 3;
    
    // Test 4: deeply nested
    int result4 = a > 10 ? 1 : (a > 5 ? 2 : (a > 0 ? 3 : 4));
    printf("test4: %d (expected 3)\n", result4);
    if (result4 != 3) return 4;
    
    printf("All tests passed!\n");
    return 0;
}
