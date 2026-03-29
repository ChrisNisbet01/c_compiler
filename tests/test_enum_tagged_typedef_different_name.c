
/* Test: Tagged enum where enum tag differs from typedef name */
enum Color
{
    RED,
    GREEN,
    BLUE
};

typedef enum Color ColorType;

int
main()
{
    enum Color c = RED;
    ColorType ct = GREEN;

    return c + ct - 1;
}
