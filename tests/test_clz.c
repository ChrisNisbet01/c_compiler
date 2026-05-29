int
main()
{
    if (__builtin_clz(1) != 31)
    {
        return 1;
    }

    if (__builtin_clz(2) != 30)
    {
        return 2;
    }

    if (__builtin_clz(8) != 28)
    {
        return 3;
    }

    if (__builtin_clz(0x80000000u) != 0)
    {
        return 4;
    }

    /* With is_zero_undef=false, LLVM returns bitwidth (32) for clz(0) */
    if (__builtin_clz(0) != 32)
    {
        return 5;
    }

    return 0;
}
