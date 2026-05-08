typedef struct Foo Foo;

typedef struct Foo {
    int x;
    Foo * self_ptr;
} Foo;

int main(void)
{
    Foo f;
    f.x = 42;
    return f.x - 42;
}
