int
add(int a, int b)
{
    return a + b;
}

int
apply_fn(int (*func_cb)(int, int), int x, int y)
{
    return func_cb(x, y);
}

int
main()
{
    int (*operation)(int, int);

    operation = add;
    int result1 = apply_fn(operation, 5, 3);
    if (result1 != 8)
        return 1;

    return 0;
}
