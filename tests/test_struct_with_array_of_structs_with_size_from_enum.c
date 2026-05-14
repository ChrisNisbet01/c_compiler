typedef enum
{
    VAL1,
    COUNT__,
} val_t;

typedef struct
{
    int val;
} entry_t;

typedef struct
{
    int i;
    entry_t entries[COUNT__];
} struct_t;

int
main(void)
{
    struct_t s = {0};
    s.entries[VAL1].val = 1;

    return 0;
}
