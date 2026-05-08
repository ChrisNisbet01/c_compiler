struct foo;

struct foo
{
    union
    {
        int a;
        char b;
    };
};

void func(struct foo * p);

void
func(struct foo * p)
{
    return;
}

int
main()
{
    struct foo f;
    func(&f);
    return 0;
}
