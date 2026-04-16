#include <stdio.h>

static int arr[3];

int
main(void)
{
    // All elements should be zero-initialized
    if (arr[0] == 0 && arr[1] == 0 && arr[2] == 0)
    {
        printf("zero init ok\n");
        return 0;
    }
    printf("non‑zero\n");
    return 1;
}
