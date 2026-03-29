typedef enum MyEnumTag
{
    ENUM_VALUE_1,
    ENUM_VALUE_2,
} MyEnum;

enum MyEnum2Tag
{
    ENUM2_VALUE_1,
    ENUM2_VALUE_2,
};

typedef enum
{
    ANON_ENUM1_VALUE_1,
    ANON_ENUM1_VALUE_2,
} AnonEnum1;

typedef struct MyStructTag MyStruct;

typedef union MyUnionTag MyUnion;

typedef int MyInt;

int
main()
{
    return 0;
}
