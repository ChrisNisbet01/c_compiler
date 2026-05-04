int main()
{
    int x = 5;
    int * const * p;
    int * ptr = &x;
    p = &ptr;
    *p = &x; /* This is assigning to a const pointer to int. Should fail. */
    return 0;
}
