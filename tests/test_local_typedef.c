int main(void)
{
    typedef int myint;
    myint x = 42;
    myint y = x + 1;

    {
        typedef float myfloat;
        myfloat f = 3.0f;
        x = (myint)f;
    }

    /* After inner block, myfloat is no longer a typedef name */
    /* But myint still is */
    myint z = x + y;
    int expected = 3 + 43;

    return (z != expected);
}
