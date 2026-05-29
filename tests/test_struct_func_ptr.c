struct runqueue;

struct runqueue_task
{
    int value;
};

struct runqueue
{
    int (*complete)(struct runqueue * q, struct runqueue_task * t);
    int extra;
};

static int
my_complete(struct runqueue * q, struct runqueue_task * t)
{
    (void)q;

    return t->value * 2;
}

int
main()
{
    struct runqueue q;
    struct runqueue_task t;
    t.value = 7;
    q.complete = my_complete;
    q.extra = 42;

    if (q.complete(&q, &t) != 14)
    {
        return 1;
    }

    if (q.extra != 42)
    {
        return 2;
    }

    return 0;
}
