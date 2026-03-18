#include <stdio.h>

int
add_int(int a, int b)
{
    return a + b;
}
float
add_float(float a, float b)
{
    return a + b;
}
// Add a function with a different signature, e.g., returning void
void
greet()
{
    printf("Greetings!\n");
}

int
main()
{
    printf("Int add: %d\n", add_int(5, 10));           // Expected: 15
    printf("Float add: %f\n", add_float(5.5f, 10.1f)); // Expected: 15.6
    greet();
    return 0;
}
