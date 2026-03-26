#include <stdio.h>

/* Test 1: Basic scope shadowing */
int test_basic_shadowing()
{
    int x = 10;
    if (x > 0)
    {
        int x = 20;  /* shadows outer x */
        if (x == 20)
        {
            return 1;  /* inner x is visible */
        }
        return 0;
    }
    return (x == 10) ? 1 : 0;  /* outer x is visible again */
}

/* Test 2: Nested blocks */
int test_nested_blocks()
{
    int result = 0;
    int a = 1;
    {
        int a = 2;
        {
            int a = 3;
            result = result + a;  /* a = 3 */
        }
        result = result + a;  /* a = 2 */
    }
    result = result + a;  /* a = 1 */
    return (result == 6) ? 1 : 0;  /* 3 + 2 + 1 = 6 */
}

/* Test 3: Function parameter shadowing */
int test_param_shadowing(int local_var)
{
    return (local_var == 50) ? 1 : 0;
}

/* Test 4: Local variable in if/else */
int test_if_else_scope()
{
    int x = 5;
    if (x > 0)
    {
        int y = 10;
        x = x + y;
    }
    /* y should not be accessible here */
    return (x == 15) ? 1 : 0;
}

/* Test 5: For loop scope */
int test_for_loop_scope()
{
    int sum = 0;
    for (int i = 0; i < 5; i++)
    {
        int temp = i * 2;
        sum = sum + temp;
    }
    /* i and temp should not be accessible here */
    return (sum == 20) ? 1 : 0;  /* 0+2+4+6+8 = 20 */
}

/* Test 6: While loop scope */
int test_while_loop_scope()
{
    int count = 0;
    int sum = 0;
    while (count < 3)
    {
        int temp = count * 10;
        sum = sum + temp;
        count++;
    }
    return (sum == 30) ? 1 : 0;  /* 0 + 10 + 20 = 30 */
}

int main()
{
    int failures = 0;

    if (test_basic_shadowing() != 1)
    {
        printf("FAIL: basic_shadowing\n");
        failures++;
    }
    else
    {
        printf("PASS: basic_shadowing\n");
    }

    if (test_nested_blocks() != 1)
    {
        printf("FAIL: nested_blocks\n");
        failures++;
    }
    else
    {
        printf("PASS: nested_blocks\n");
    }

    if (test_param_shadowing(50) != 1)
    {
        printf("FAIL: param_shadowing\n");
        failures++;
    }
    else
    {
        printf("PASS: param_shadowing\n");
    }

    if (test_if_else_scope() != 1)
    {
        printf("FAIL: if_else_scope\n");
        failures++;
    }
    else
    {
        printf("PASS: if_else_scope\n");
    }

    if (test_for_loop_scope() != 1)
    {
        printf("FAIL: for_loop_scope\n");
        failures++;
    }
    else
    {
        printf("PASS: for_loop_scope\n");
    }

    if (test_while_loop_scope() != 1)
    {
        printf("FAIL: while_loop_scope\n");
        failures++;
    }
    else
    {
        printf("PASS: while_loop_scope\n");
    }

    if (failures == 0)
    {
        printf("All scope tests passed!\n");
        return 0;
    }
    return failures;
}
