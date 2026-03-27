int
main()
{
    int a = 1, *b, c = 2;
    b = &a;
    int result = *b + c;
    return result - 3;
}
