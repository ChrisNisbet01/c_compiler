#include <stdio.h>

typedef struct
{
    int i;
} AStruct_t;

int
main()
{
    AStruct_t s;
    s.i = 0;
    return s.i;
}
