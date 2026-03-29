
/* Test: Mix of tagged enum and typedef usage */
enum Color { RED, GREEN, BLUE };

typedef enum Color Color;

int
main()
{
    enum Color c1 = RED;
    Color c2 = GREEN;
    return c1 + c2 - 1;
}
