static inline int add(int a, int b)
{
    return a + b;
}

static inline int multiply(int a, int b)
{
    return a * b;
}

int main(void)
{
    int x = add(3, 4);
    int y = multiply(x, 2);
    return y - 14; /* 7 * 2 = 14, so return 0 on success */
}
