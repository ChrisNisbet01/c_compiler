#include <stdio.h>

int main()
{
    int const x = 10;
    x = 20; // This should trigger a "Cannot assign to const variable" error
    return 0;
}
