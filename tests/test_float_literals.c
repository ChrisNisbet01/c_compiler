#include <stdio.h>

int
main()
{
    float f1 = 1.5f;
    double d1 = 2.5;
    long double ld1 = 3.0L;
    printf("float: %f, double: %f, long double: %Lf\n", f1, d1, ld1);
    // Check for correct literal parsing and type mapping.
    return 0;
}
