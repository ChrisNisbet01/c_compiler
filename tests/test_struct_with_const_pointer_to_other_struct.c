struct runqueue_task_type
{
    char const * name;
};

struct runqueue_task
{
    const struct runqueue_task_type * type;
};

int
main()
{
    struct runqueue_task_type my_task_type = {.name = "MyTask"};
    struct runqueue_task my_task = {.type = &my_task_type};

    return my_task.type->name[0] == 'M' ? 0 : 1;
}
