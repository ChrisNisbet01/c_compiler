// Test case - basic cast expression
typedef struct
{
    int x;
    int y;
} Point;

Point global_point = {10, 20};
Point * global_ptr = (Point *)&global_point;

int
main()
{
    // Cast the global pointer
    int b = global_ptr->y;

    return b - 20;
}
