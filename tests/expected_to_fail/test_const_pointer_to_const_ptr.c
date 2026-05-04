int main()
{
    int x = 5;
    int const * const p = &x;
    int y = 10;
    p = &y;   /* Should fail: pointer is const */
    *p = y;   /* Should fail: pointee is const */
    return 0;
}
