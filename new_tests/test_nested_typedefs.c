typedef signed char __int8_t;
typedef __int8_t __int_least8_t;
typedef __int8_t * CharPtr;

int
main()
{
    __int8_t c = 5;
    CharPtr pc = &c;

    return *pc - 5;
}
