// Test case - cast a global pointer variable
typedef struct
{
    int x;
    int y;
} Point;

Point global_point = {10, 20};
Point * global_ptr = &global_point;

int
main()
{
    int b = global_ptr->y;

    return b - 20;
}