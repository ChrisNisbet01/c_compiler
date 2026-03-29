
/* Test: Verify typedef enum is equivalent to enum tag */
enum Color
{
    RED = 1,
    GREEN = 2,
    BLUE = 4
};

typedef enum Color ColorType;

int
main()
{
    /* Declare using enum tag */
    enum Color c1 = RED;
    enum Color c2 = BLUE;

    /* Declare using typedef */
    ColorType c3 = GREEN;
    ColorType c4 = RED;

    /* Values should be identical regardless of declaration method */
    return c1 + c2 + c3 + c4 - 8;
}
