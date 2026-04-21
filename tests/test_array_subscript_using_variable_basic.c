char arr[5];

int
main()
{
    arr[0] = 'a';
    arr[1] = 'b';
    arr[2] = '\0';

    int i = 1;

    return arr[i] != 'b';
}
