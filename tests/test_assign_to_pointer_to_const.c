int
main(void)
{
    char const * s1 = "abc";
    char const * s2 = "def";
    char * const s3 = s3;

    s2 = s1;

    return !(s2[0] == 'a' && s2[1] == 'b' && s2[2] == 'c');
}