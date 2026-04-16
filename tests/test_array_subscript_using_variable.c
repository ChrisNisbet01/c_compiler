#include <stdio.h>

char arr[20];

int
main()
{
    arr[0] = 'a';
    arr[1] = 'b';
    arr[2] = '\0';

    printf("%s\n", arr);
    int i = 1;

    return arr[i] != 'b';
}
