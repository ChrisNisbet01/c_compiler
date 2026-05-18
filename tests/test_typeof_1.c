// #include <stddef.h>

int
main()
{
    int i = 1;
    typeof(i) j = 2;
    typeof(int) k = i;
    typeof(int *) pi = &i;

    return j + *pi - 3;
}
