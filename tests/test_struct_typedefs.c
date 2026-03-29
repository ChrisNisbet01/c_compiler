
typedef struct StructTag TagStruct;

typedef struct
{
    int i;
} UntaggedTypedef;

typedef struct TaggedStruct
{
    int i;
} TaggedTypedef;

struct UntypedStruct
{
    int i;
};

struct EmptyStruct;

int
main()
{
    struct UntypedStruct us;
    us.i = 5;
    TaggedTypedef ts;
    ts.i = 10;

    return us.i + ts.i - 15;
}
