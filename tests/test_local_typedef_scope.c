int main(void)
{
    int result = 0;

    /* Define a typedef inside a nested block */
    {
        typedef int inner_int;
        inner_int x = 42;
        result += x;
    }

    /* After the block, inner_int should NOT be visible as a typedef.
       The name 'result' should not be confused with any leaked typedef. */
    result += 1;

    /* Verify a global-scope typedef still works correctly */
    typedef int global_int;
    global_int y = 100;
    result += y;

    return (result != 143);
}
