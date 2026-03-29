int foo()
{
    struct Point { int x; int y; };
    struct Point p;
    p.x = 10;
    p.y = 20;
    return p.x + p.y;
}

int bar()
{
    struct Point { int a; int b; int c; };
    struct Point p;
    p.a = 1;
    p.b = 2;
    p.c = 3;
    return p.a + p.b + p.c;
}

int main()
{
    int foo_result = foo();
    int bar_result = bar();
    return (foo_result + bar_result) - 36;
}
