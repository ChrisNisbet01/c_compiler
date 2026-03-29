
/* Test: Forward declaration of enum with typedef */
typedef enum Status Status;

enum Status
{
    OK = 0,
    ERROR = 1
};

int
main()
{
    Status s = OK;
    return s;
}
