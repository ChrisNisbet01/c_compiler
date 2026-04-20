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
    printf("sizeof(char): %zu\n", sizeof(char));
    printf("sizeof(long): %zu\n", sizeof(long));
    printf("sizeof(ptr): %zu\n", sizeof(ptr));
    printf("sizeof(*ptr): %zu\n", sizeof(*ptr));
    printf("sizeof(x): %zu\n", sizeof(x));
    printf("sizeof(arr): %zu\n", sizeof(arr));
    printf("sizeof(struct Point): %zu\n", sizeof(struct Point));
    printf("sizeof(struct Padded): %zu\n", sizeof(struct Padded));
    printf("sizeof(p): %zu\n", sizeof(p));

    printf("\n=== alignof tests ===\n");

    printf("alignof(int): %zu\n", __alignof__(int));
    printf("alignof(char): %zu\n", __alignof__(char));
    printf("alignof(long): %zu\n", __alignof__(long));
    printf("alignof(ptr): %zu\n", __alignof__(ptr));
    printf("alignof(struct Point): %zu\n", __alignof__(struct Point));
    printf("alignof(struct Padded): %zu\n", __alignof__(struct Padded));

    if (sizeof(int) != 4)
        return 1;
    if (sizeof(char) != 1)
        return 2;
    if (sizeof(long) != 8)
        return 3;
    if (sizeof(ptr) != 8)
        return 4;
    if (sizeof(*ptr) != 4)
        return 5;
    if (sizeof(x) != 4)
        return 6;
    if (sizeof(arr) != 40)
        return 7;
    if (sizeof(struct Point) != 8)
        return 8;
    if (sizeof(struct Padded) != 12)
        return 9;
    if (sizeof(p) != 8)
        return 10;

    if (__alignof__(int) != 4)
        return 11;
    if (__alignof__(char) != 1)
        return 12;
    if (__alignof__(long) != 4)
        return 13;
    if (__alignof__(ptr) != 8)
        return 14;
    if (__alignof__(struct Point) != 4)
        return 15;
    if (__alignof__(struct Padded) != 4)
        return 16;

    return 0;
}
