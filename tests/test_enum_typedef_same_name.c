
/* Test: Tagged enum with typedef (same name) */
typedef enum Color { RED, GREEN, BLUE } Color;

int
main()
{
    Color c = GREEN;
    return c - 1;
}
