#include <stdio.h>

struct Point
{
    int x;
    int y;
};

struct Padded
{
    char c;
    int i;
    char d;
};

int
main()
{
    struct Point p;
    struct Padded pad;
    int arr[10];
    int x;
    int * ptr = &x;

    printf("=== sizeof tests ===\n");

    printf("sizeof(int): %zu\n", sizeof(int));

    printf("\n=== alignof tests ===\n");

    printf("alignof(int): %zu\n", __alignof__(int));
    printf("alignof(long): %zu\n", __alignof__(long));

    if (sizeof(int) != 4)
        return 1;

    if (__alignof__(int) != 4)
        return 11;
    if (__alignof__(char) != 1)
        return 12;
    if (__alignof__(long) != 4)
        return 13;

    return 0;
}
