int main()
{
    int x = 5;
    int y = 10;
    int * const p = &x;
    p = &y; /* Should fail: p is a const pointer */
    return 0;
}
