#include <stdio.h>

typedef enum
{
    A = 10,
    B = 20,
    BIT_AND = 10 & 6,
    BIT_OR = 10 | 6,
    BIT_XOR = 10 ^ 6,
    SHIFT_L = 1 << 3,
    SHIFT_R = 32 >> 2,
    ADD = 5 + 3,
    SUB = 10 - 4,
    MUL = 3 * 7,
    DIV = 20 / 4,
    MOD = 21 % 8,
    EQ_EQ = 5 == 5,
    NE = 5 != 3,
    LT = 3 < 5,
    GT = 5 > 3,
    LE = 3 <= 3,
    GE = 5 >= 5,
    CHAIN = (10 & 3) + (1 << 2),
} Expr;

int
main()
{
    Expr e;

    e = BIT_AND;
    printf("BIT_AND: %d (expected 10 & 6 = 2)\n", e);
    if (e != 2)
        return 1;

    e = BIT_OR;
    printf("BIT_OR: %d (expected 10 | 6 = 14)\n", e);
    if (e != 14)
        return 2;

    e = BIT_XOR;
    printf("BIT_XOR: %d (expected 10 ^ 6 = 12)\n", e);
    if (e != 12)
        return 3;

    e = SHIFT_L;
    printf("SHIFT_L: %d (expected 1 << 3 = 8)\n", e);
    if (e != 8)
        return 4;

    e = SHIFT_R;
    printf("SHIFT_R: %d (expected 32 >> 2 = 8)\n", e);
    if (e != 8)
        return 5;

    e = ADD;
    printf("ADD: %d (expected 5 + 3 = 8)\n", e);
    if (e != 8)
        return 6;

    e = SUB;
    printf("SUB: %d (expected 10 - 4 = 6)\n", e);
    if (e != 6)
        return 7;

    e = MUL;
    printf("MUL: %d (expected 3 * 7 = 21)\n", e);
    if (e != 21)
        return 8;

    e = DIV;
    printf("DIV: %d (expected 20 / 4 = 5)\n", e);
    if (e != 5)
        return 9;

    e = MOD;
    printf("MOD: %d (expected 21 % 8 = 5)\n", e);
    if (e != 5)
        return 10;

    e = EQ_EQ;
    printf("EQ_EQ: %d (expected 5 == 5 = 1)\n", e);
    if (e != 1)
        return 11;

    e = NE;
    printf("NE: %d (expected 5 != 3 = 1)\n", e);
    if (e != 1)
        return 12;

    e = LT;
    printf("LT: %d (expected 3 < 5 = 1)\n", e);
    if (e != 1)
        return 13;

    e = GT;
    printf("GT: %d (expected 5 > 3 = 1)\n", e);
    if (e != 1)
        return 14;

    e = LE;
    printf("LE: %d (expected 3 <= 3 = 1)\n", e);
    if (e != 1)
        return 15;

    e = GE;
    printf("GE: %d (expected 5 >= 5 = 1)\n", e);
    if (e != 1)
        return 16;

    e = CHAIN;
    printf("CHAIN: %d (expected (10 & 3) + (1 << 2) = 2 + 4 = 6)\n", e);
    if (e != 6)
        return 17;

    return 0;
}