/* Test: Invalid type specifier - should fail at parsing/compilation */

/* int is not valid in this context - missing semicolon creates syntax error */
int main()
{
    int x = 5
    return x;
}