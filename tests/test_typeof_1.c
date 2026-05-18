// #include <stddef.h>

int
main()
{
    int i;
    typeof(i) j = 2;
    typeof(int) k = i;
    return j - 2;
}
