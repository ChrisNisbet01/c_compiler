
/* Test: Untagged enum via typedef */
typedef enum { RED, GREEN, BLUE } Color;

int
main()
{
    Color c = RED;
    return c;
}
