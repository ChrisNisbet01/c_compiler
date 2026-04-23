int
add(int a, int b)
{
    return a + b;
}

int
main()
{
    /* Array of function pointers */
    int (*ops[1])(int, int) = {add};
    int result5 = ops[0](100, 50);
    if (result5 != 150)
        return 5;

    return 0;
}
