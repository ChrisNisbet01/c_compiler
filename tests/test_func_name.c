#include <stdio.h>

int
main()
{
    char * func_name = __func__;
    printf("Function name: %s\n", func_name);
    return func_name == NULL;
}
