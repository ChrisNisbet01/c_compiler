#include <stdio.h>

typedef struct AStruct_st
{
    int i;
} AStruct_t;

int
main()
{
    struct AStruct_t s;
    s.i = 0;
    return s.i;
}
