int main()
{
    int x = 5;
    const int * p = &x;
    int y = 10;
    *p = y; /* Should fail: pointee is const */
    return 0;
}
