struct S
{
    int i;
    int i1;
    int i2;
    int i3;
    int i4;
    int i5;
    int i6;
};

static int
ret_i(struct S * ps)
{
    return ps->i;
}

int
main(void)
{
    struct S s = {1};
    int v = ret_i(&s);
    return v - 1;
}
