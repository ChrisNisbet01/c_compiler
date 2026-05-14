typedef enum
{
    VAL1 = 2,
} val_t;

typedef struct
{
    int val;
} entry_t;

static void
copy_val(entry_t * e, val_t * v)
{
    *v = e->val;
}

int
main(void)
{
    entry_t e;
    e.val = VAL1;
    val_t v;

    copy_val(&e, &v);

    return e.val - v;
}
