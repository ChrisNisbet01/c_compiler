int
main()
{
    // Basic compound expression: last expression is the value
    int x = ({
        int a = 10;
        a + 5;
    });
    if (x != 15)
    {
        return 1;
    }
    // Multiple declarations, use a variable from inside the block
    int y = ({
        int tmp = 100;
        int result = tmp * 2;
        result;
    });
    if (y != 200)
    {
        return 2;
    }
    // Compound expression in a larger expression context (binary op)
    int z = 5 + ({ 10; });
    if (z != 15)
    {
        return 3;
    }
    // Nested compound expressions
    int w = ({ ({ 42; }); });
    if (w != 42)
    {
        return 4;
    }
    // Compound expression as a ternary operand
    int v = 1 ? ({ 7; }) : ({ 99; });
    if (v != 7)
    {
        return 5;
    }
    return 0;
}
