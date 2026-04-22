#include <stdio.h>

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
apply(int (*func)(int, int), int x, int y)
{
    return func(x, y);
}

int
main()
{
    int (*operation)(int, int);

    operation = add;
    int result1 = apply(operation, 5, 3);
    printf("add(5, 3) = %d\n", result1);
    if (result1 != 8)
        return 1;

    operation = subtract;
    int result2 = apply(operation, 5, 3);
    printf("subtract(5, 3) = %d\n", result2);
    if (result2 != 2)
        return 2;

    /* Direct function pointer call */
    int (*fp)(int, int) = add;
    int result3 = fp(10, 20);
    printf("fp(10, 20) = %d\n", result3);
    if (result3 != 30)
        return 3;

    /* Call through function pointer with subtract */
    fp = subtract;
    int result4 = fp(100, 50);
    printf("fp(100, 50) = %d (subtract)\n", result4);
    if (result4 != 50)
        return 4;

    /* Array of function pointers */
    int (*ops[2])(int, int) = {add, subtract};
    int result5 = ops[0](100, 50);
    int result6 = ops[1](100, 50);
    printf("ops[0](100, 50) = %d\n", result5);
    printf("ops[1](100, 50) = %d\n", result6);
    if (result5 != 150)
        return 5;
    if (result6 != 50)
        return 6;

    printf("All function pointer tests passed!\n");
    return 0;
}
