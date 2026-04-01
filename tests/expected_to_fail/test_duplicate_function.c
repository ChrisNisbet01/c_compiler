/* Test: Duplicate function definition - should fail at code generation */

int
foo()
{
    return 1;
}

int
foo() /* Redefinition */
{
    return 2;
}

int
main()
{
    return 0;
}
