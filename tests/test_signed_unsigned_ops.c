#include <stdio.h>

int
main(void)
{
    int result = 0;

    // Test signed division
    int signed_a = -10;
    int signed_b = 3;
    if (signed_a / signed_b != -3)
    {
        printf("FAIL: signed division: %d / %d = %d, expected -3\n", signed_a, signed_b, signed_a / signed_b);
        result = 1;
    }

    // Test signed remainder
    if (signed_a % signed_b != -1)
    {
        printf("FAIL: signed remainder: %d %% %d = %d, expected -1\n", signed_a, signed_b, signed_a % signed_b);
        result = 2;
    }

    // Test unsigned division (using unsigned long to ensure unsigned)
    unsigned long unsigned_a = 10u;
    unsigned long unsigned_b = 3u;
    if (unsigned_a / unsigned_b != 3)
    {
        printf(
            "FAIL: unsigned division: %zu / %zu = %zu, expected 3\n", unsigned_a, unsigned_b, unsigned_a / unsigned_b
        );
        result = 3;
    }

    // Test unsigned remainder
    if (unsigned_a % unsigned_b != 1)
    {
        printf(
            "FAIL: unsigned remainder: %zu %% %zu = %zu, expected 1\n", unsigned_a, unsigned_b, unsigned_a % unsigned_b
        );
        result = 4;
    }

    // Test signed comparison
    if (!(signed_a < signed_b))
    {
        printf("FAIL: signed less than: %d < %d = %d, expected 1\n", signed_a, signed_b, signed_a < signed_b);
        result = 5;
    }

    // Test unsigned comparison (large values that would be negative if signed)
    unsigned long large_unsigned = (unsigned long)2000000000u; // 2 billion
    unsigned long small_unsigned = 1000u;
    if (!(large_unsigned > small_unsigned))
    {
        printf(
            "FAIL: unsigned greater than: %zu > %zu = %d, expected 1\n",
            large_unsigned,
            small_unsigned,
            large_unsigned > small_unsigned
        );
        result = 6;
    }

    // Test unsigned comparison that would fail if treated as signed
    unsigned long val1 = 100;
    unsigned long val2 = 200;
    if (val1 >= val2)
    {
        printf("FAIL: unsigned >= : %zu >= %zu = %d, expected 0\n", val1, val2, val1 >= val2);
        result = 7;
    }

    // Test that a large unsigned value doesn't become negative
    unsigned long very_large = (unsigned long)0xFFFFFFFFFFFFFFFFULL; // Max unsigned long
    if ((int)very_large > 0) // This would be negative if cast to int incorrectly
    {
        printf("FAIL: large unsigned long should not be negative when interpreted as signed\n");
        result = 8;
    }

    // Test signed with negative results
    int neg1 = -5;
    int neg2 = -2;
    if (neg1 / neg2 != 2)
    {
        printf("FAIL: negative division: %d / %d = %d, expected 2\n", neg1, neg2, neg1 / neg2);
        result = 9;
    }

    if (neg1 % neg2 != -1)
    {
        printf("FAIL: negative remainder: %d %% %d = %d, expected -1\n", neg1, neg2, neg1 % neg2);
        result = 10;
    }

    // Test that unsigned modulo with large dividend works correctly
    unsigned long big_dividend = 0xFFFFFFFFu; // Large but fits in 32 bits
    unsigned long divisor = 1000u;
    unsigned long rem = big_dividend % divisor;
    if (rem != 295)
    {
        printf("FAIL: large unsigned remainder: %zu %% %zu = %zu, expected 535\n", big_dividend, divisor, rem);
        result = 11;
    }

    if (result == 0)
    {
        printf("All signed/unsigned tests PASSED\n");
    }

    return result;
}