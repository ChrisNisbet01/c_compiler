typedef struct
{
    int x;
} mytype;

int
take_struct(mytype s)
{
    return s.x;
}

int
main()
{
    int i = take_struct((mytype){42});
    return i - 42;
}
