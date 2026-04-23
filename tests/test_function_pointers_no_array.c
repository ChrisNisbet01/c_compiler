int
add(int a, int b)
{
    return a + b;
}

int
subtract(int a, int b)
{
    return a - b;
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

    operation = subtract;
    int result2 = apply_fn(operation, 5, 3);
    printf("subtract(5, 3) = %d\n", result2);
    if (result2 != 2)
        return 2;

    /* Direct function pointer call */
    int (*fp)(int, int) = add;
    int result3 = fp(10, 20);
    if (result3 != 30)
        return 3;

    /* Call through function pointer with subtract */
    fp = subtract;
    int result4 = fp(100, 50);
    if (result4 != 50)
        return 4;

    return 0;
}
